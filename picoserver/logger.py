import machine
import time
import os
import json
import asyncio
from sdcard import SDCard
import ws_uart

UART_ID = 0
UART_BAUDRATE = 115200
UART_TX_PIN = 0
UART_RX_PIN = 1
MAX_UART_LINE_BYTES = 240

SD_MOUNT = "/sd"
UART_TEXT_LOG_FILE = SD_MOUNT + "/uart_log.txt"
SD_INIT_BAUDRATES = (1320000, 1000000, 400000, 100000)

led = machine.Pin("LED", machine.Pin.OUT)
led.value(0)

uart = machine.UART(
    UART_ID,
    baudrate=UART_BAUDRATE,
    tx=machine.Pin(UART_TX_PIN),
    rx=machine.Pin(UART_RX_PIN),
)

spi = machine.SPI(
    0,
    baudrate=1000000,
    polarity=0,
    phase=0,
    sck=machine.Pin(18),
    mosi=machine.Pin(19),
    miso=machine.Pin(16),
)
cs = machine.Pin(17, machine.Pin.OUT)

sd = None
sd_connected = False


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
        source = status[4:].replace(",", "_").replace(" ", "")
        return "ERR:" + _sanitize_error_source(source)

    return "WARN"


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
        return entry
    except Exception:
        return None


def _station_log_file(station_id):
    return SD_MOUNT + "/log_" + station_id + ".json"


def _is_sd_available():
    try:
        os.listdir(SD_MOUNT)
        return True
    except OSError:
        return False


def _set_sd_connected(connected, message=None):
    global sd_connected
    sd_connected = connected
    led.value(1 if connected else 0)
    if message:
        print(message)


def _unmount_sd():
    try:
        os.umount(SD_MOUNT)
    except OSError:
        pass


def _mount_sd():
    global sd

    for baudrate in SD_INIT_BAUDRATES:
        try:
            _unmount_sd()
            time.sleep_ms(200)
            card = SDCard(spi, cs, baudrate=baudrate)
            try:
                os.mount(card, SD_MOUNT)
            except OSError:
                os.listdir(SD_MOUNT)
            sd = card
            print("SD mounted at", SD_MOUNT, "(baudrate:", baudrate, ")")
            return True
        except OSError as e:
            print("SD init failed (baudrate", baudrate, "):", e)

    sd = None
    return False


def _verify_sd_write_ready():
    if not _is_sd_available():
        return False

    probe_path = SD_MOUNT + "/.write_check"
    try:
        with open(probe_path, "w") as f:
            f.write("ok")
        os.remove(probe_path)
        return True
    except OSError as e:
        print("SD write probe failed:", e)
        return False


def _mark_sd_disconnected(reason):
    global sd
    if sd_connected:
        print("SD disconnected:", reason)
    _set_sd_connected(False)
    _unmount_sd()
    sd = None


def _ensure_sd_ready():
    if sd_connected:
        if not _is_sd_available():
            _mark_sd_disconnected("card removed")
        return

    if not _mount_sd():
        return

    if _verify_sd_write_ready():
        _set_sd_connected(True, "LED ON: SD connected and writable.")
    else:
        _mark_sd_disconnected("write probe failed")


def _write_line(path, line):
    if not sd_connected:
        return

    try:
        with open(path, "a") as f:
            f.write(line + "\n")
    except OSError as e:
        _mark_sd_disconnected("write error: " + str(e))


def _log_uart_line(line):
    _write_line(UART_TEXT_LOG_FILE, line)


def _log_measurement(station_id, entry):
    payload = {"station_id": station_id, "timestamp": entry["timestamp"], "status": entry["status"]}
    for field_name in ws_uart.SENSOR_FIELDS:
        payload[field_name] = entry.get(field_name)
    _write_line(_station_log_file(station_id), json.dumps(payload))


def _handle_uart_line(line):
    if ws_uart.is_uart_control_line(line):
        return

    try:
        station_id, payload = ws_uart.parse_measurement_line(line)
        entry = _format_entry(payload)
        if entry is None:
            raise ValueError("invalid data payload")
        _log_measurement(station_id, entry)
    except ws_uart.NotMeasurementFrameError:
        _log_uart_line(line)
    except Exception as e:
        print("UART parse error:", e, "| line:", line)
        _log_uart_line("PARSE_ERROR: " + str(e) + " | " + line)


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
                                    _handle_uart_line(line)
                        else:
                            if len(buffer) < MAX_UART_LINE_BYTES:
                                buffer.append(byte)
                            else:
                                buffer = bytearray()
                                print("UART overflow: line dropped")
        except Exception as e:
            print("UART task error:", e)

        await asyncio.sleep_ms(50)


async def sd_monitor_task():
    while True:
        _ensure_sd_ready()
        await asyncio.sleep_ms(1000)


async def main():
    ws_uart.self_check()
    _ensure_sd_ready()
    asyncio.create_task(sd_monitor_task())
    await uart_reading_task()


try:
    asyncio.run(main())
finally:
    led.value(0)
    _unmount_sd()
    print("LED OFF: program stopped.")
