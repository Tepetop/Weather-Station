import network
import machine
import time
import os
import json
import asyncio
import gc
from microdot import Microdot, Response, send_file
from sdcard import SDCard
import aggregation
import ws_uart

# Wi-Fi configuration
SSID = "Miminet"
PASSWORD = "21590801"

# Data mode
SIMULATE = False # Set to True to enable simulation mode (no real sensors)

# UART configuration (Pico W UART1: GP4 TX, GP5 RX)
UART_ID = 0
UART_BAUDRATE = 115200
UART_TX_PIN = 0
UART_RX_PIN = 1
MAX_UART_LINE_BYTES = 240
UART_EXCHANGE_TIMEOUT_S = 5

# API and storage settings
RANGE_SECONDS = {
    "1h": 3600,
    "3h": 10800,
    "1d": 86400,
    "1w": 604800,
}
DEFAULT_RANGE = "1h"
MAX_API_POINTS = 120
MAX_LOG_VIEW_ROWS = 60
RAM_LIMIT_PER_STATION = 60
RAM_LOG_LIMIT = 20
AGGREGATE_SECONDS = {"10m": 600, "1h": 3600}
RANGE_RESOLUTION = {"1h": "10m", "3h": "10m", "1d": "1h", "1w": "1h"}
AGGREGATE_KEEP_DAYS = {"10m": 2, "1h": 8}
FIELD_DISPLAY_STEPS = {
    "si7021_temp": 0.5,
    "si7021_hum": 1,
    "bmp280_temp": 0.5,
    "bmp280_press": 0.5,
    "tsl2561_lux": 10,
    "bme280_temp": 0.5,
    "bme280_press": 0.5,
    "bme280_hum": 1,
}
QUANTITY_FIELDS = {
    "temperature": ("si7021_temp", "bmp280_temp", "bme280_temp"),
    "humidity": ("si7021_hum", "bme280_hum"),
    "pressure": ("bmp280_press", "bme280_press"),
    "illuminance": ("tsl2561_lux",),
}
QUANTITY_DISPLAY_STEPS = {
    "temperature": 0.5,
    "humidity": 1,
    "pressure": 0.5,
    "illuminance": 10,
}

# Hardware
led = machine.Pin("LED", machine.Pin.OUT)
led.value(0)
uart = machine.UART(
    UART_ID,
    baudrate=UART_BAUDRATE,
    tx=machine.Pin(UART_TX_PIN),
    rx=machine.Pin(UART_RX_PIN),
)

# SD card (SPI0)
spi = machine.SPI(0,
                  baudrate=1000000,
                  polarity=0,
                  phase=0,
                  sck=machine.Pin(18),
                  mosi=machine.Pin(19),
                  miso=machine.Pin(16))
cs = machine.Pin(17, machine.Pin.OUT)

SD_MOUNT = "/sd"
LEGACY_LOG_FILE = SD_MOUNT + "/log.json"
UART_TEXT_LOG_FILE = SD_MOUNT + "/uart_log.txt"
SD_INIT_BAUDRATES = (1320000, 1000000, 400000, 100000)
SD_INIT_MAX_ATTEMPTS = 3
RAM_LOGS = []
sd = None
SD_WRITE_READY = False
SD_INIT_DISABLED = False

STATION_DATA = {}
AGGREGATE_STATE = {}
_aggregate_cleanup_days = {}
_uart_response_line = None
_uart_cmd_lock = None
_api_heavy_lock = None
_MEM_FREE_MIN = None
_sd_unavailable_count = 0
_sd_last_error = None


def _get_uart_cmd_lock():
    global _uart_cmd_lock
    if _uart_cmd_lock is None:
        _uart_cmd_lock = asyncio.Lock()
    return _uart_cmd_lock


def _get_api_heavy_lock():
    global _api_heavy_lock
    if _api_heavy_lock is None:
        _api_heavy_lock = asyncio.Lock()
    return _api_heavy_lock


def _note_mem():
    global _MEM_FREE_MIN
    free = gc.mem_free()
    if _MEM_FREE_MIN is None or free < _MEM_FREE_MIN:
        _MEM_FREE_MIN = free
    return free


def _append_ram_log(entry):
    RAM_LOGS.append(entry)
    if len(RAM_LOGS) > RAM_LOG_LIMIT:
        RAM_LOGS.pop(0)


def _safe_station_id(raw_station_id):
    return ws_uart.safe_station_id(raw_station_id)


def _station_log_file(station_id):
    return SD_MOUNT + "/log_" + station_id + ".json"


def _aggregate_file(resolution, station_id, day_key):
    return (
        SD_MOUNT + "/series_" + resolution + "_" + station_id + "_"
        + day_key + ".json"
    )


def _aggregate_prefix(resolution, station_id):
    return "series_" + resolution + "_" + station_id + "_"


def _is_sd_available():
    try:
        os.listdir(SD_MOUNT)
        return True
    except OSError:
        return False


def _unmount_sd():
    try:
        os.umount(SD_MOUNT)
    except OSError:
        pass


def _mark_sd_unavailable(reason=None):
    global sd, SD_WRITE_READY
    if reason:
        print("SD disconnected:", reason)
    SD_WRITE_READY = False
    _unmount_sd()
    sd = None


def _normalize_range(range_key):
    if range_key in RANGE_SECONDS:
        return range_key
    return DEFAULT_RANGE


def _parse_limit(raw_limit, fallback):
    try:
        limit = int(raw_limit)
        if limit < 1:
            return fallback
        if limit > MAX_API_POINTS:
            return MAX_API_POINTS
        return limit
    except Exception:
        return fallback


def _parse_non_negative_int(raw_value, fallback=0):
    try:
        value = int(raw_value)
        if value < 0:
            return fallback
        return value
    except Exception:
        return fallback


def _sanitize_error_source(raw_source):
    source = str(raw_source).upper().strip()
    if not source:
        return "UNKNOWN"

    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
    cleaned = ""
    for ch in source:
        if ch in allowed:
            cleaned += ch

    if not cleaned:
        return "UNKNOWN"
    return cleaned[:24]


def _normalize_status(raw_status):
    status = str(raw_status).upper().strip()

    if status == "OK":
        return "OK"
    if status == "WARN":
        return "WARN"
    if status == "ERR":
        return "ERR:UNKNOWN"
    if status.startswith("ERR:"):
        # STM32 may send ERR:SI7021,BMP280,TSL2561
        source = status[4:].replace(",", "_").replace(" ", "")
        return "ERR:" + _sanitize_error_source(source)

    return "WARN"


def _timestamp_to_epoch(timestamp):
    if not isinstance(timestamp, str) or len(timestamp) != 19:
        return None
    if timestamp[4] != "-" or timestamp[7] != "-" or timestamp[10] != "T":
        return None
    if timestamp[13] != ":" or timestamp[16] != ":":
        return None

    try:
        year = int(timestamp[0:4])
        month = int(timestamp[5:7])
        day = int(timestamp[8:10])
        hour = int(timestamp[11:13])
        minute = int(timestamp[14:16])
        second = int(timestamp[17:19])
    except ValueError:
        return None

    try:
        return int(time.mktime((year, month, day, hour, minute, second, 0, 0)))
    except Exception:
        return None


def _epoch_to_timestamp(epoch):
    value = time.localtime(int(epoch))
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}".format(
        value[0], value[1], value[2], value[3], value[4], value[5]
    )


def _timestamp_day_key(timestamp):
    return timestamp[0:10].replace("-", "")


def _format_sensor_value(field_name, raw_value):
    if raw_value is None:
        return None
    if field_name == "tsl2561_lux":
        return int(float(raw_value))
    if field_name in ("bmp280_press", "bme280_press"):
        return round(float(raw_value), 2)
    return round(float(raw_value), 1)


def _format_entry(payload):
    try:
        entry = {
            "timestamp": str(payload["timestamp"]),
            "status": _normalize_status(payload["status"]),
        }
        for field_name in ws_uart.SENSOR_FIELDS:
            entry[field_name] = _format_sensor_value(field_name, payload.get(field_name))
    except Exception:
        return None, None

    epoch = _timestamp_to_epoch(entry["timestamp"])
    if epoch is None:
        return None, None
    return entry, epoch


def _parse_uart_measurement_line(line):
    station_id, payload = ws_uart.parse_measurement_line(line)
    entry, _ = _format_entry(payload)
    if entry is None:
        raise ValueError("invalid data payload")
    return station_id, entry


def _deliver_uart_response(line):
    global _uart_response_line
    if _uart_response_line is None:
        _uart_response_line = line


async def uart_exchange(cmd_line, timeout_s=UART_EXCHANGE_TIMEOUT_S):
    global _uart_response_line

    async with _get_uart_cmd_lock():
        _uart_response_line = None

        uart.write((cmd_line + "\r\n").encode())
        try:
            uart.flush()
        except AttributeError:
            pass

        print("UART TX:", cmd_line)

        deadline = time.ticks_add(time.ticks_ms(), int(timeout_s * 1000))
        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            if _uart_response_line is not None:
                print("UART RX:", _uart_response_line)
                return _uart_response_line
            await asyncio.sleep_ms(10)

        print("UART timeout waiting for response to:", cmd_line)
        raise asyncio.TimeoutError()


def _verify_sd_write_ready():
    if not _is_sd_available():
        return False

    probe_path = SD_MOUNT + "/writecheck.tmp"
    try:
        with open(probe_path, "w") as f:
            f.write("ok")
        try:
            os.remove(probe_path)
        except OSError as e:
            # Write path works; leftover probe file is non-fatal.
            print("SD probe cleanup failed:", e)
        return True
    except OSError as e:
        print("SD write probe failed:", e)
        _append_ram_log({"kind": "sd_probe_error", "error": str(e)})
        return False


def mount_sd():
    for baudrate in SD_INIT_BAUDRATES:
        try:
            _unmount_sd()
            time.sleep_ms(250)
            card = SDCard(spi, cs, baudrate=baudrate)
            os.mount(card, SD_MOUNT)
            if _verify_sd_write_ready():
                print("Karta SD zamontowana w", SD_MOUNT, "(baudrate:", baudrate, ")")
                return card
            print("SD zapis nieudany (baudrate", baudrate, ") — sprobuj nizszy.")
        except OSError as e:
            print("Init SD nieudany (baudrate", baudrate, "):", e)

        _unmount_sd()
        time.sleep_ms(400)

    return None


def _init_sd_at_startup():
    global sd, SD_WRITE_READY, SD_INIT_DISABLED

    for attempt in range(1, SD_INIT_MAX_ATTEMPTS + 1):
        print("Proba montowania karty SD:", attempt, "/", SD_INIT_MAX_ATTEMPTS)
        card = mount_sd()
        if card is not None:
            sd = card
            SD_WRITE_READY = True
            print("SD connected and writable.")
            return True

        if attempt < SD_INIT_MAX_ATTEMPTS:
            time.sleep_ms(800)

    sd = None
    SD_WRITE_READY = False
    SD_INIT_DISABLED = True
    print(
        "Karta SD niedostepna po",
        SD_INIT_MAX_ATTEMPTS,
        "probach. Praca bez zapisu na SD.",
    )
    return False


def _ensure_sd_ready():
    global sd, SD_WRITE_READY

    if SD_INIT_DISABLED and not SD_WRITE_READY:
        return

    if SD_WRITE_READY:
        if not _is_sd_available():
            _mark_sd_unavailable("card removed")
        return

    card = mount_sd()
    if card is None:
        sd = None
        SD_WRITE_READY = False
        return

    sd = card
    SD_WRITE_READY = True
    print("SD connected and writable.")


def remount_sd():
    """Manual re-init after failed startup mount (clears SD_INIT_DISABLED)."""
    global SD_INIT_DISABLED

    print("Reczna reinicjalizacja czytnika SD...")
    _mark_sd_unavailable("manual remount")
    SD_INIT_DISABLED = False
    ok = _init_sd_at_startup()
    led.value(1 if ok else 0)
    return ok


def _append_station_entry(station_id, entry):
    entries = STATION_DATA.get(station_id)
    if entries is None:
        entries = []
        STATION_DATA[station_id] = entries

    entries.append(entry)
    if len(entries) > RAM_LIMIT_PER_STATION:
        entries.pop(0)


def log_station_to_sd(station_id, entry):
    global _sd_unavailable_count, _sd_last_error

    payload = {"station_id": station_id, "timestamp": entry["timestamp"], "status": entry["status"]}
    for field_name in ws_uart.SENSOR_FIELDS:
        payload[field_name] = entry.get(field_name)

    if not _is_sd_available():
        _sd_unavailable_count += 1
        _sd_last_error = "sd_unavailable"
        return

    try:
        with open(_station_log_file(station_id), "a") as f:
            f.write(json.dumps(payload) + "\n")
    except OSError as e:
        _sd_unavailable_count += 1
        _sd_last_error = str(e)
        _append_ram_log({"kind": "sd_write_error", "error": str(e)})


def _aggregate_file_names(resolution, station_id):
    if not _is_sd_available():
        return []
    prefix = _aggregate_prefix(resolution, station_id)
    try:
        return sorted(
            name
            for name in os.listdir(SD_MOUNT)
            if name.startswith(prefix) and name.endswith(".json")
        )
    except OSError:
        return []


def _cleanup_aggregate_files(resolution, station_id, current_day):
    cleanup_key = resolution + ":" + station_id
    if _aggregate_cleanup_days.get(cleanup_key) == current_day:
        return
    _aggregate_cleanup_days[cleanup_key] = current_day

    names = _aggregate_file_names(resolution, station_id)
    keep = AGGREGATE_KEEP_DAYS[resolution]
    for name in names[:-keep]:
        try:
            os.remove(SD_MOUNT + "/" + name)
        except OSError as e:
            _append_ram_log({
                "kind": "aggregate_cleanup_error",
                "error": str(e),
            })


def _append_aggregate_record(resolution, station_id, record):
    if not _is_sd_available():
        return False
    day_key = _timestamp_day_key(record["timestamp"])
    try:
        with open(_aggregate_file(resolution, station_id, day_key), "a") as f:
            f.write(json.dumps(record) + "\n")
        _cleanup_aggregate_files(resolution, station_id, day_key)
        return True
    except OSError as e:
        _append_ram_log({
            "kind": "aggregate_write_error",
            "error": str(e),
        })
        return False


def _iter_aggregate_records(resolution, station_id):
    names = _aggregate_file_names(resolution, station_id)
    keep = AGGREGATE_KEEP_DAYS[resolution]
    for name in names[-keep:]:
        try:
            with open(SD_MOUNT + "/" + name, "r") as f:
                for line in f:
                    try:
                        record = json.loads(line)
                    except Exception:
                        continue
                    if isinstance(record, dict) and "_bucket" in record:
                        yield record
        except OSError:
            continue


def _restore_hour_bucket(station_id, start_epoch):
    bucket = aggregation.new_bucket(start_epoch)
    for record in _iter_aggregate_records("10m", station_id):
        if aggregation.bucket_start(record.get("_bucket", 0), 3600) == start_epoch:
            aggregation.merge_record(bucket, record, ws_uart.SENSOR_FIELDS)
    return bucket


def _update_hour_aggregate(station_id, record):
    state = AGGREGATE_STATE.setdefault(station_id, {})
    start_epoch = aggregation.bucket_start(record["_bucket"], 3600)
    bucket = state.get("1h")
    if bucket is None:
        bucket = _restore_hour_bucket(station_id, start_epoch)
        state["1h"] = bucket
    elif start_epoch > bucket["_bucket"]:
        finished = aggregation.finalize_bucket(
            bucket,
            _epoch_to_timestamp(bucket["_bucket"]),
            ws_uart.SENSOR_FIELDS,
        )
        _append_aggregate_record("1h", station_id, finished)
        bucket = aggregation.new_bucket(start_epoch)
        state["1h"] = bucket
    elif start_epoch < bucket["_bucket"]:
        return
    aggregation.merge_record(bucket, record, ws_uart.SENSOR_FIELDS)


def _update_aggregates(station_id, entry):
    epoch = _timestamp_to_epoch(entry.get("timestamp", ""))
    if epoch is None:
        return

    state = AGGREGATE_STATE.setdefault(station_id, {})
    start_epoch = aggregation.bucket_start(epoch, AGGREGATE_SECONDS["10m"])
    bucket = state.get("10m")
    if bucket is None:
        bucket = aggregation.new_bucket(start_epoch)
        state["10m"] = bucket
    elif start_epoch > bucket["_bucket"]:
        finished = aggregation.finalize_bucket(
            bucket,
            _epoch_to_timestamp(bucket["_bucket"]),
            ws_uart.SENSOR_FIELDS,
        )
        _update_hour_aggregate(station_id, finished)
        _append_aggregate_record("10m", station_id, finished)
        bucket = aggregation.new_bucket(start_epoch)
        state["10m"] = bucket
    elif start_epoch < bucket["_bucket"]:
        return

    aggregation.add_sample(
        bucket, entry, ws_uart.SENSOR_FIELDS, entry.get("status", "WARN")
    )


def _handle_measurement_line(line):
    station_id, entry = _parse_uart_measurement_line(line)
    _append_station_entry(station_id, entry)
    log_station_to_sd(station_id, entry)
    _update_aggregates(station_id, entry)


def log_uart_line_to_sd(line):
    if not _is_sd_available():
        return
    try:
        with open(UART_TEXT_LOG_FILE, "a") as f:
            f.write(line + "\n")
    except OSError as e:
        _append_ram_log({"kind": "uart_log_write_error", "error": str(e)})


def _entry_from_json_line(line):
    try:
        payload = json.loads(line)
    except Exception:
        return None, None

    if not isinstance(payload, dict):
        return None, None

    entry, epoch = _format_entry(payload)
    if entry is None:
        return None, None
    return entry, epoch


def _load_aggregate_data(station_id, range_key, max_points=MAX_API_POINTS):
    resolution = RANGE_RESOLUTION[range_key]
    window_seconds = RANGE_SECONDS[range_key]
    rows = []
    latest_epoch = None

    for record in _iter_aggregate_records(resolution, station_id):
        epoch = int(record.get("_bucket", 0))
        if epoch < 1:
            continue
        latest_epoch = epoch if latest_epoch is None else max(latest_epoch, epoch)
        rows.append((
            epoch,
            aggregation.public_record(
                record, ws_uart.SENSOR_FIELDS, FIELD_DISPLAY_STEPS
            ),
        ))
        cutoff = latest_epoch - window_seconds
        while rows and rows[0][0] < cutoff:
            rows.pop(0)

    if latest_epoch is None:
        return []

    cutoff = latest_epoch - window_seconds
    rows = [item for item in rows if item[0] >= cutoff]
    if len(rows) > max_points:
        stride = (len(rows) + max_points - 1) // max_points
        sampled = rows[::stride]
        if sampled[-1] != rows[-1]:
            sampled.append(rows[-1])
        rows = sampled
    return [item[1] for item in rows]


def _insert_recent_log_entry(entries, entry, max_entries):
    timestamp = entry.get("timestamp", "")
    inserted = False

    for index in range(len(entries)):
        if timestamp > entries[index].get("timestamp", ""):
            entries.insert(index, entry)
            inserted = True
            break

    if not inserted:
        entries.append(entry)

    if len(entries) > max_entries:
        entries.pop()


def _iter_sd_log_entries(station_id):
    if not _is_sd_available():
        return

    try:
        with open(_station_log_file(station_id), "r") as f:
            for line in f:
                entry, _ = _entry_from_json_line(line)
                if entry is None:
                    continue
                if "station_id" not in entry:
                    entry["station_id"] = station_id
                yield entry
    except OSError:
        return


def _load_recent_sd_logs(station_filter, offset, limit):
    if not _is_sd_available():
        return 0, []

    station_ids = get_station_ids() if station_filter == "all" else [station_filter]
    max_entries = offset + limit
    total = 0
    recent = []

    for station_id in station_ids:
        for entry in _iter_sd_log_entries(station_id):
            total += 1
            _insert_recent_log_entry(recent, entry, max_entries)

    gc.collect()
    return total, recent[offset:offset + limit]


def _api_data_json_bytes(station, range_key, limit, rows):
    chunks = [
        b'{"station":',
        json.dumps(station).encode(),
        b',"range":',
        json.dumps(range_key).encode(),
        b',"resolution":',
        json.dumps(RANGE_RESOLUTION[range_key]).encode(),
        b',"limit":',
        str(limit).encode(),
        b',"count":',
        str(len(rows)).encode(),
        b',"data":[',
    ]
    for i, row in enumerate(rows):
        if i:
            chunks.append(b',')
        chunks.append(json.dumps(row).encode())
        rows[i] = None
        if (i & 15) == 15:
            gc.collect()
    chunks.append(b']}')
    body = b"".join(chunks)
    chunks = None
    return body


def _stream_csv(rows):
    yield (
        "timestamp,si7021_temp,si7021_hum,bmp280_temp,bmp280_press,tsl2561_lux,"
        "bme280_temp,bme280_press,bme280_hum,status\n"
    )
    for i, row in enumerate(rows):
        yield "{},{},{},{},{},{},{},{},{},{}\n".format(
            row["timestamp"],
            row.get("si7021_temp", ""),
            row.get("si7021_hum", ""),
            row.get("bmp280_temp", ""),
            row.get("bmp280_press", ""),
            row.get("tsl2561_lux", ""),
            row.get("bme280_temp", ""),
            row.get("bme280_press", ""),
            row.get("bme280_hum", ""),
            row["status"],
        )
        rows[i] = None
        if (i & 15) == 15:
            gc.collect()


def _station_ids_from_sd():
    if not _is_sd_available():
        return []

    station_ids = []
    try:
        for name in os.listdir(SD_MOUNT):
            if name.startswith("log_") and name.endswith(".json"):
                station_id = _safe_station_id(name[4:-5])
                if station_id:
                    station_ids.append(station_id)
    except OSError:
        return []
    return station_ids


def get_station_ids():
    station_ids = {}
    for station_id in STATION_DATA.keys():
        station_ids[station_id] = True
    for station_id in _station_ids_from_sd():
        station_ids[station_id] = True
    return sorted(station_ids.keys())


def _latest_entry_from_sd(station_id):
    if not _is_sd_available():
        return None

    latest = None
    try:
        with open(_station_log_file(station_id), "r") as f:
            for line in f:
                entry, _ = _entry_from_json_line(line)
                if entry is not None:
                    latest = entry
    except OSError:
        return None
    return latest


def _latest_entry(station_id):
    entries = STATION_DATA.get(station_id, [])
    if entries:
        return entries[-1]
    return _latest_entry_from_sd(station_id)


def connect_wifi(ssid, password, timeout_s=30):
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        print("Laczenie z Wi-Fi...")
        wlan.connect(ssid, password)
        start = time.ticks_ms()
        while not wlan.isconnected():
            if time.ticks_diff(time.ticks_ms(), start) > timeout_s * 1000:
                raise RuntimeError("Wi-Fi: timeout polaczenia")
            time.sleep(0.2)
    print("Polaczono! IP:", wlan.ifconfig()[0])
    return wlan


# Microdot app
app = Microdot()


@app.route("/")
async def index(request):
    gc.collect()
    try:
        return send_file(
            "index.html",
            content_type="text/html; charset=utf-8",
            max_age=0,
        )
    except OSError as e:
        print("Nie udalo sie otworzyc index.html:", e)
        return ("<html><body><h1>Pico W</h1><p>Brak index.html</p></body></html>",
                200, {"Content-Type": "text/html; charset=utf-8"})


@app.route("/api/device")
async def api_device(request):
    gc.collect()
    free = _note_mem()
    return {
        "mem_free": free,
        "mem_alloc": gc.mem_alloc(),
        "mem_free_min": _MEM_FREE_MIN,
        "sd_available": _is_sd_available(),
        "sd_write_ready": SD_WRITE_READY,
        "sd_unavailable_count": _sd_unavailable_count,
        "sd_last_error": _sd_last_error,
        "ram_stations": len(STATION_DATA),
        "ram_log_count": len(RAM_LOGS),
    }


@app.route("/api/stations")
async def api_stations(request):
    stations = get_station_ids()
    return {
        "stations": stations,
        "count": len(stations),
    }


@app.route("/api/sensors")
async def api_sensors(request):
    return {
        "fields": [
            dict(field=name, **ws_uart.SENSOR_FIELD_INFO[name])
            for name in ws_uart.SENSOR_FIELDS
        ]
    }


@app.route("/api/latest")
async def api_latest(request):
    async with _get_api_heavy_lock():
        _note_mem()
        rows = []
        for station_id in get_station_ids():
            entry = _latest_entry(station_id)
            if entry is None:
                continue
            rows.append({
                "station_id": station_id,
                "timestamp": entry["timestamp"],
                "si7021_temp": entry["si7021_temp"],
                "si7021_hum": entry["si7021_hum"],
                "bmp280_temp": entry["bmp280_temp"],
                "bmp280_press": entry["bmp280_press"],
                "tsl2561_lux": entry["tsl2561_lux"],
                "bme280_temp": entry["bme280_temp"],
                "bme280_press": entry["bme280_press"],
                "bme280_hum": entry["bme280_hum"],
                "status": entry["status"],
            })

        rows.sort(key=lambda row: row["station_id"])
        _note_mem()
        return {
            "count": len(rows),
            "data": rows,
        }


@app.route("/api/current")
async def api_current(request):
    station = _safe_station_id(request.args.get("station", ""))
    if not station:
        return {"error": "invalid station id"}, 400

    entries = STATION_DATA.get(station, [])[-10:]
    if not entries:
        return {"error": "no recent data for station"}, 404

    values = aggregation.average_recent(entries, QUANTITY_FIELDS)
    return {
        "station": station,
        "timestamp": entries[-1].get("timestamp", ""),
        "sample_count": len(entries),
        "status": aggregation.worst_status(entries),
        "temperature": aggregation.quantize(
            values["temperature"], QUANTITY_DISPLAY_STEPS["temperature"]
        ),
        "humidity": aggregation.quantize(
            values["humidity"], QUANTITY_DISPLAY_STEPS["humidity"]
        ),
        "pressure": aggregation.quantize(
            values["pressure"], QUANTITY_DISPLAY_STEPS["pressure"]
        ),
        "illuminance": aggregation.quantize(
            values["illuminance"], QUANTITY_DISPLAY_STEPS["illuminance"]
        ),
    }


@app.route("/api/data")
async def api_data(request):
    station = _safe_station_id(request.args.get("station", ""))
    range_key = _normalize_range(request.args.get("range", DEFAULT_RANGE))
    output_format = request.args.get("format", "json").lower()
    limit = _parse_limit(request.args.get("limit", ""), MAX_API_POINTS)
    if not station:
        return {"error": "invalid station id"}, 400

    async with _get_api_heavy_lock():
        gc.collect()
        _note_mem()

        try:
            rows = _load_aggregate_data(station, range_key, limit)
        except MemoryError:
            gc.collect()
            print("/api/data OOM during aggregate load")
            rows = []

        if len(rows) > limit:
            rows = rows[-limit:]

        gc.collect()
        _note_mem()

        if output_format == "csv":
            def body():
                for chunk in _stream_csv(rows):
                    yield chunk

            return Response(
                body=body(),
                headers={"Content-Type": "text/csv; charset=utf-8"},
            )

        try:
            body = _api_data_json_bytes(station, range_key, limit, rows)
        except MemoryError:
            gc.collect()
            rows = rows[-20:]
            body = _api_data_json_bytes(station, range_key, limit, rows)

        rows = None
        gc.collect()
        return Response(
            body=body,
            headers={"Content-Type": "application/json; charset=UTF-8"},
        )


@app.route("/api/trigger", methods=["POST"])
async def api_trigger(request):
    gc.collect()
    if _get_uart_cmd_lock().locked():
        return {"status": "busy", "error": "uart exchange in progress"}, 409

    node_raw = request.args.get("node", "")
    try:
        if node_raw == "":
            cmd = ws_uart.build_measure_cmd()
        else:
            node = int(node_raw)
            if node < 0 or node > 3:
                return {"status": "error", "error": "invalid node"}, 400
            cmd = ws_uart.build_measure_cmd(node)

        response = await uart_exchange(cmd)
        response = str(response).strip()

        if response == "ACK:MEASURE:QUEUED":
            return {"status": "queued", "command": cmd, "response": response}
        if response == "ERR:BUSY":
            return {"status": "busy", "command": cmd, "response": response}, 409
        if response == "ERR:UNKNOWN":
            return {"status": "error", "command": cmd, "response": response}, 400

        return {"status": "unexpected", "command": cmd, "response": response}, 502
    except asyncio.TimeoutError:
        return {"status": "timeout", "command": cmd if "cmd" in locals() else ""}, 504
    except Exception as e:
        return {"status": "error", "error": str(e)}, 500


@app.route("/api/ping")
async def api_ping(request):
    gc.collect()
    if _get_uart_cmd_lock().locked():
        return {"status": "busy", "stm32": "busy"}, 409

    try:
        response = await uart_exchange(ws_uart.CMD_PING, timeout_s=2)
        response = str(response).strip()
        if response == "ACK:PING":
            return {"status": "ok", "stm32": "online", "response": response}
        return {"status": "unexpected", "response": response}, 502
    except asyncio.TimeoutError:
        return {"status": "timeout", "stm32": "offline"}, 504
    except Exception as e:
        return {"status": "error", "error": str(e)}, 500


@app.route("/api/logs")
async def api_logs(request):
    station = request.args.get("station", "all")
    limit = _parse_limit(request.args.get("limit", ""), MAX_LOG_VIEW_ROWS)
    if limit > MAX_LOG_VIEW_ROWS:
        limit = MAX_LOG_VIEW_ROWS
    offset = _parse_non_negative_int(request.args.get("offset", ""), 0)

    if station != "all":
        station = _safe_station_id(station)
        if not station:
            return {"error": "invalid station id"}, 400

    async with _get_api_heavy_lock():
        _note_mem()
        total, rows = _load_recent_sd_logs(station, offset, limit)
        _note_mem()
        return {
            "station": station,
            "offset": offset,
            "limit": limit,
            "count": len(rows),
            "total": total,
            "sd_available": _is_sd_available(),
            "data": rows,
        }


@app.route("/api/sd/remount", methods=["POST"])
async def api_sd_remount(request):
    async with _get_api_heavy_lock():
        gc.collect()
        ok = remount_sd()
        payload = {
            "status": "ok" if ok else "failed",
            "sd_available": _is_sd_available(),
            "sd_write_ready": SD_WRITE_READY,
            "sd_init_disabled": SD_INIT_DISABLED,
        }
        if ok:
            return payload
        return payload, 503


@app.route("/shutdown")
async def shutdown(request):
    request.app.shutdown()
    return "Serwer zostal zatrzymany..."


@app.route("/logs")
async def show_logs(request):
    async with _get_api_heavy_lock():
        limit = MAX_LOG_VIEW_ROWS
        if _is_sd_available():
            try:
                recent = []
                with open(LEGACY_LOG_FILE, "r") as f:
                    for line in f:
                        line = line.strip()
                        if not line:
                            continue
                        try:
                            recent.append(json.loads(line))
                        except Exception:
                            continue
                        if len(recent) > limit:
                            recent.pop(0)
                if recent:
                    return recent
            except OSError:
                pass
        return RAM_LOGS[-limit:]


async def uart_reading_task():
    buffer = bytearray()
    while True:
        try:
            if uart.any():
                chunk = uart.read()
                if chunk:
                    for byte in chunk:
                        if byte == 10 or byte == 13:
                            if buffer:
                                try:
                                    line = buffer.decode().strip()
                                except Exception:
                                    line = ""
                                buffer = bytearray()

                                if line:
                                    if ws_uart.is_uart_control_line(line):
                                        _deliver_uart_response(line)
                                        continue

                                    try:
                                        _handle_measurement_line(line)
                                    except ws_uart.NotMeasurementFrameError:
                                        _append_ram_log({"kind": "uart_log", "line": line})
                                        if ws_uart.is_uart_log_line(line):
                                            log_uart_line_to_sd(line)
                                    except Exception as parse_error:
                                        _append_ram_log({
                                            "kind": "uart_parse_error",
                                            "error": str(parse_error),
                                            "line": line,
                                        })
                                        print("UART parse error:", parse_error, "| line:", line)
                        else:
                            if len(buffer) < MAX_UART_LINE_BYTES:
                                buffer.append(byte)
                            else:
                                buffer = bytearray()
                                _append_ram_log({"kind": "uart_overflow"})
        except Exception as e:
            print("UART task error:", e)
        await asyncio.sleep_ms(50)


async def sd_monitor_task():
    was_ready = SD_WRITE_READY
    while True:
        if SD_WRITE_READY or not SD_INIT_DISABLED:
            _ensure_sd_ready()
        if SD_WRITE_READY and not was_ready:
            led.value(1)
            print("LED ON: SD connected and writable.")
        elif not SD_WRITE_READY and was_ready:
            led.value(0)
            print("LED OFF: SD unavailable.")
        was_ready = SD_WRITE_READY
        await asyncio.sleep_ms(1000)


# Start
ws_uart.self_check()
_init_sd_at_startup()
wlan = connect_wifi(SSID, PASSWORD)
gc.collect()
if SD_INIT_DISABLED:
    print("Kontynuacja pracy bez karty SD.")
elif not SD_WRITE_READY:
    print("Karta SD zamontowana, ale test zapisu nie powiodl sie.")
print("Uruchamiam serwer Microdot na porcie 80...")


async def main():
    asyncio.create_task(sd_monitor_task())
    if SIMULATE:
        try:
            import simulate
            print("Tryb symulacji wlaczony.")
            asyncio.create_task(simulate.simulate_task(_handle_measurement_line, interval_s=60))
        except Exception as e:
            print("Nie udalo sie uruchomic symulacji:", e)
            asyncio.create_task(uart_reading_task())
    else:
        asyncio.create_task(uart_reading_task())

    server_task = asyncio.create_task(
        app.start_server(host="0.0.0.0", port=80, debug=True))
    await asyncio.sleep_ms(300)

    if SD_WRITE_READY and not server_task.done():
        led.value(1)
        print("LED ON: serwer i zapis SD gotowe.")
    else:
        led.value(0)

    try:
        await server_task
    finally:
        led.value(0)
        print("LED OFF: serwer zatrzymany.")


asyncio.run(main())


