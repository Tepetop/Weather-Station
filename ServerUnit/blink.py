import network
import machine
import time
import os
import json
import asyncio
import gc
from microdot import Microdot, redirect
from sdcard import SDCard

# Wi-Fi configuration
SSID = "Miminet"
PASSWORD = "21590801"

# Data mode
SIMULATE = True

# UART configuration (Pico W UART0: GP0 TX, GP1 RX)
UART_ID = 0
UART_BAUDRATE = 9600
UART_TX_PIN = 0
UART_RX_PIN = 1
MAX_UART_LINE_BYTES = 240

# API and storage settings
RANGE_SECONDS = {
    "1h": 3600,
    "3h": 10800,
    "1d": 86400,
    "1w": 604800,
}
DEFAULT_RANGE = "1h"
MAX_API_POINTS = 120
MAX_API_AVG_POINTS = 90
RAM_LIMIT_PER_STATION = 180
SD_SCAN_CAP_BY_RANGE = {
    "1h": 180,
    "3h": 360,
    "1d": 900,
    "1w": 1200,
}

# Hardware
led = machine.Pin("LED", machine.Pin.OUT)
sensor = machine.ADC(4)
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
SD_INIT_BAUDRATES = (1320000, 1000000, 400000, 100000)
RAM_LOGS = []
RAM_LOG_LIMIT = 200
sd = None

STATION_DATA = {}
INDEX_HTML_BYTES = b""


def _append_ram_log(entry):
    RAM_LOGS.append(entry)
    if len(RAM_LOGS) > RAM_LOG_LIMIT:
        RAM_LOGS.pop(0)


def _load_index_html_once():
    try:
        with open("index.html", "rb") as f:
            content = f.read()
        print("index.html cached:", len(content), "bytes")
        return content
    except OSError as e:
        print("Nie udalo sie zaladowac index.html:", e)
        return b"<html><body><h1>Pico W</h1><p>Brak index.html</p></body></html>"


def _safe_station_id(raw_station_id):
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
    sanitized = ""
    for ch in raw_station_id:
        if ch in allowed:
            sanitized += ch
    return sanitized[:16]


def _station_log_file(station_id):
    return SD_MOUNT + "/log_" + station_id + ".json"


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


def _normalize_status(raw_status):
    status = str(raw_status).upper().strip()
    if status in ("OK", "WARN", "ERR"):
        return status
    return "WARN"


def _status_rank(status):
    if status == "ERR":
        return 2
    if status == "WARN":
        return 1
    return 0


def _worst_status(left, right):
    if _status_rank(right) > _status_rank(left):
        return right
    return left


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


def _downsample_entries(entries, max_points=MAX_API_POINTS):
    total = len(entries)
    if total <= max_points:
        return entries

    stride = (total + max_points - 1) // max_points
    sampled = []
    for i in range(total):
        if i % stride == 0:
            sampled.append(entries[i])

    if sampled and sampled[-1] != entries[-1]:
        sampled.append(entries[-1])
    return sampled


def _format_entry(payload):
    try:
        entry = {
            "timestamp": str(payload["timestamp"]),
            "si7021_temp": round(float(payload["si7021_temp"]), 1),
            "si7021_hum": round(float(payload["si7021_hum"]), 1),
            "bmp280_temp": round(float(payload["bmp280_temp"]), 1),
            "bmp280_press": round(float(payload["bmp280_press"]), 2),
            "tsl2561_lux": int(float(payload["tsl2561_lux"])),
            "status": _normalize_status(payload["status"]),
        }
    except Exception:
        return None, None

    epoch = _timestamp_to_epoch(entry["timestamp"])
    if epoch is None:
        return None, None
    return entry, epoch


def _parse_uart_measurement_line(line):
    parts = line.strip().split(",")
    if len(parts) != 8:
        raise ValueError("expected 8 CSV fields")

    payload = {
        "timestamp": parts[0].strip(),
        "si7021_temp": parts[2].strip(),
        "si7021_hum": parts[3].strip(),
        "bmp280_temp": parts[4].strip(),
        "bmp280_press": parts[5].strip(),
        "tsl2561_lux": parts[6].strip(),
        "status": parts[7].strip(),
    }

    station_id = _safe_station_id(parts[1].strip())
    if not station_id:
        raise ValueError("invalid station_id")

    entry, _ = _format_entry(payload)
    if entry is None:
        raise ValueError("invalid data payload")
    return station_id, entry


def mount_sd():
    for baudrate in SD_INIT_BAUDRATES:
        try:
            time.sleep_ms(200)
            card = SDCard(spi, cs, baudrate=baudrate)
            try:
                os.mount(card, SD_MOUNT)
            except OSError:
                # Accept already-mounted card as long as mount point is readable.
                os.listdir(SD_MOUNT)
            print("Karta SD zamontowana w", SD_MOUNT, "(baudrate:", baudrate, ")")
            return card
        except OSError as e:
            print("Init SD nieudany (baudrate", baudrate, "):", e)

    print("Brak dostepu do SD. Start bez zapisu na karte.")
    return None


def _append_station_entry(station_id, entry):
    entries = STATION_DATA.get(station_id)
    if entries is None:
        entries = []
        STATION_DATA[station_id] = entries

    entries.append(entry)
    if len(entries) > RAM_LIMIT_PER_STATION:
        entries.pop(0)


def log_station_to_sd(station_id, entry):
    payload = {
        "station_id": station_id,
        "timestamp": entry["timestamp"],
        "si7021_temp": entry["si7021_temp"],
        "si7021_hum": entry["si7021_hum"],
        "bmp280_temp": entry["bmp280_temp"],
        "bmp280_press": entry["bmp280_press"],
        "tsl2561_lux": entry["tsl2561_lux"],
        "status": entry["status"],
    }

    if sd is None:
        _append_ram_log({"kind": "sd_unavailable", "line": payload})
        return

    try:
        with open(_station_log_file(station_id), "a") as f:
            f.write(json.dumps(payload) + "\n")
    except OSError as e:
        _append_ram_log({"kind": "sd_write_error", "error": str(e), "line": payload})


def _handle_measurement_line(line):
    station_id, entry = _parse_uart_measurement_line(line)
    _append_station_entry(station_id, entry)
    log_station_to_sd(station_id, entry)


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


def _load_station_data_from_ram(station_id, range_key):
    entries = STATION_DATA.get(station_id, [])
    if not entries:
        return []

    parsed = []
    latest_epoch = None
    for entry in entries:
        epoch = _timestamp_to_epoch(entry.get("timestamp", ""))
        if epoch is None:
            continue
        parsed.append((epoch, entry))
        if latest_epoch is None or epoch > latest_epoch:
            latest_epoch = epoch

    if latest_epoch is None:
        return []

    cutoff = latest_epoch - RANGE_SECONDS[range_key]
    filtered = []
    for epoch, entry in parsed:
        if epoch >= cutoff:
            filtered.append(entry)
    return _downsample_entries(filtered)


def _load_station_data_from_sd(station_id, range_key):
    if sd is None:
        return []

    file_path = _station_log_file(station_id)
    window_seconds = RANGE_SECONDS[range_key]
    cap = SD_SCAN_CAP_BY_RANGE.get(range_key, MAX_API_POINTS * 4)
    rows = []
    epochs = []

    try:
        with open(file_path, "r") as f:
            for line in f:
                entry, epoch = _entry_from_json_line(line)
                if entry is None or epoch is None:
                    continue

                rows.append(entry)
                epochs.append(epoch)

                cutoff = epoch - window_seconds
                while epochs and epochs[0] < cutoff:
                    epochs.pop(0)
                    rows.pop(0)

                if len(rows) > cap:
                    rows.pop(0)
                    epochs.pop(0)
    except OSError:
        return []

    if not rows:
        return []

    return _downsample_entries(rows)


def _load_station_data(station_id, range_key):
    range_key = _normalize_range(range_key)

    sd_rows = _load_station_data_from_sd(station_id, range_key)
    ram_rows = _load_station_data_from_ram(station_id, range_key)

    if not sd_rows:
        return ram_rows
    if not ram_rows:
        return sd_rows

    latest_sd_ts = sd_rows[-1].get("timestamp", "")
    extras = []
    for row in ram_rows:
        if row.get("timestamp", "") > latest_sd_ts:
            extras.append(row)

    if extras:
        merged = sd_rows + extras
        return _downsample_entries(merged)
    return sd_rows


def _load_average_data(range_key):
    station_ids = get_station_ids()
    if not station_ids:
        return []

    buckets = {}
    for station_id in station_ids:
        rows = _load_station_data(station_id, range_key)
        for row in rows:
            ts = row["timestamp"]
            bucket = buckets.get(ts)
            if bucket is None:
                bucket = {
                    "count": 0,
                    "si7021_temp": 0.0,
                    "si7021_hum": 0.0,
                    "bmp280_temp": 0.0,
                    "bmp280_press": 0.0,
                    "tsl2561_lux": 0.0,
                    "status": "OK",
                }
                buckets[ts] = bucket

            bucket["count"] += 1
            bucket["si7021_temp"] += row["si7021_temp"]
            bucket["si7021_hum"] += row["si7021_hum"]
            bucket["bmp280_temp"] += row["bmp280_temp"]
            bucket["bmp280_press"] += row["bmp280_press"]
            bucket["tsl2561_lux"] += row["tsl2561_lux"]
            bucket["status"] = _worst_status(bucket["status"], row["status"])

    result = []
    for ts in sorted(buckets.keys()):
        bucket = buckets[ts]
        count = bucket["count"]
        if count == 0:
            continue

        result.append({
            "timestamp": ts,
            "si7021_temp": round(bucket["si7021_temp"] / count, 1),
            "si7021_hum": round(bucket["si7021_hum"] / count, 1),
            "bmp280_temp": round(bucket["bmp280_temp"] / count, 1),
            "bmp280_press": round(bucket["bmp280_press"] / count, 2),
            "tsl2561_lux": int(round(bucket["tsl2561_lux"] / count)),
            "status": bucket["status"],
        })

    return _downsample_entries(result, MAX_API_AVG_POINTS)


def _to_csv(rows):
    out = [
        "timestamp,si7021_temp,si7021_hum,bmp280_temp,bmp280_press,tsl2561_lux,status"
    ]
    for row in rows:
        out.append(
            "{},{},{},{},{},{},{}".format(
                row["timestamp"],
                row["si7021_temp"],
                row["si7021_hum"],
                row["bmp280_temp"],
                row["bmp280_press"],
                row["tsl2561_lux"],
                row["status"],
            )
        )
    return "\n".join(out)


def _station_ids_from_sd():
    if sd is None:
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
    if sd is None:
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


def connect_wifi(ssid, password, timeout_s=15):
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


def read_temperature():
    raw = sensor.read_u16()
    voltage = raw * 3.3 / 65535
    temp_c = 27 - (voltage - 0.706) / 0.001721
    return round(temp_c, 1)


def _device_snapshot():
    return {
        "led_state": "ON" if led.value() else "OFF",
        "temperature_c": read_temperature(),
    }


# Microdot app
app = Microdot()


@app.route("/")
async def index(request):
    return INDEX_HTML_BYTES, 200, {
        "Content-Type": "text/html; charset=utf-8",
        "Cache-Control": "no-store",
    }


@app.route("/led/<action>")
async def led_control(request, action):
    if action == "on":
        led.value(1)
    elif action == "off":
        led.value(0)
    elif action == "toggle":
        led.value(not led.value())
    else:
        return {"error": "invalid action"}, 400

    if request.args.get("format", "").lower() == "json":
        payload = _device_snapshot()
        payload["action"] = action
        return payload

    return redirect("/")


@app.route("/temperature")
async def temperature(request):
    return {"temperature_c": read_temperature()}


@app.route("/api/device")
async def api_device(request):
    return _device_snapshot()


@app.route("/api/stations")
async def api_stations(request):
    stations = get_station_ids()
    return {
        "stations": stations,
        "count": len(stations),
    }


@app.route("/api/latest")
async def api_latest(request):
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
            "status": entry["status"],
        })

    rows.sort(key=lambda row: row["station_id"])
    return {
        "count": len(rows),
        "data": rows,
    }


@app.route("/api/data")
async def api_data(request):
    station = request.args.get("station", "avg")
    range_key = _normalize_range(request.args.get("range", DEFAULT_RANGE))
    output_format = request.args.get("format", "json").lower()
    limit = _parse_limit(request.args.get("limit", ""), MAX_API_POINTS)

    if station == "avg":
        rows = _load_average_data(range_key)
    else:
        station = _safe_station_id(station)
        if not station:
            return {"error": "invalid station id"}, 400
        rows = _load_station_data(station, range_key)

    if len(rows) > limit:
        rows = rows[-limit:]

    gc.collect()

    if output_format == "csv":
        return _to_csv(rows), 200, {"Content-Type": "text/csv; charset=utf-8"}

    return {
        "station": station,
        "range": range_key,
        "limit": limit,
        "count": len(rows),
        "data": rows,
    }


@app.route("/api/trigger", methods=["POST"])
async def api_trigger(request):
    try:
        uart.write("MEASURE\n")
        return {"status": "sent", "command": "MEASURE"}
    except Exception as e:
        return {"status": "error", "error": str(e)}, 500


@app.route("/shutdown")
async def shutdown(request):
    request.app.shutdown()
    return "Serwer zostal zatrzymany..."


@app.route("/logs")
async def show_logs(request):
    if sd is not None:
        try:
            with open(LEGACY_LOG_FILE, "r") as f:
                data = f.read()
            return [json.loads(line) for line in data.strip().split("\n") if line]
        except OSError:
            pass
    return RAM_LOGS


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
                                    try:
                                        _handle_measurement_line(line)
                                    except Exception as parse_error:
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


# Start
wlan = connect_wifi(SSID, PASSWORD)
sd = mount_sd()
INDEX_HTML_BYTES = _load_index_html_once()
gc.collect()
if sd is None:
    print("Kontynuacja pracy bez karty SD.")
print("Uruchamiam serwer Microdot na porcie 80...")


async def main():
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

    await app.start_server(host="0.0.0.0", port=80, debug=True)


asyncio.run(main())


