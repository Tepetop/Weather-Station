"""Weather Station UART protocol: tagged DATA lines and CMD/ACK text commands."""

UART_DATA_PREFIX = "DATA:"
UART_LOG_PREFIXES = ("LOG:", "INFO:", "DBG:", "TRACE:", "SYS:")
UART_CONTROL_PREFIXES = ("ACK:", "ERR:")

CMD_MEASURE = "CMD:MEASURE"
CMD_PING = "CMD:PING"

CHANNEL_FIELDS = {
    0x01: "si7021_temp",
    0x02: "si7021_hum",
    0x03: "bmp280_temp",
    0x04: "bmp280_press",
    0x05: "tsl2561_lux",
    0x06: "bme280_temp",
    0x07: "bme280_press",
    0x08: "bme280_hum",
}

SENSOR_FIELDS = (
    "si7021_temp",
    "si7021_hum",
    "bmp280_temp",
    "bmp280_press",
    "tsl2561_lux",
    "bme280_temp",
    "bme280_press",
    "bme280_hum",
)

SENSOR_FIELD_INFO = {
    "si7021_temp": {"sensor": "Si7021", "quantity": "Temperatura", "unit": "\u00b0C"},
    "si7021_hum": {"sensor": "Si7021", "quantity": "Wilgotno\u015b\u0107", "unit": "%"},
    "bmp280_temp": {"sensor": "BMP280", "quantity": "Temperatura", "unit": "\u00b0C"},
    "bmp280_press": {"sensor": "BMP280", "quantity": "Ci\u015bnienie", "unit": "hPa"},
    "tsl2561_lux": {"sensor": "TSL2561", "quantity": "Nat\u0119\u017cenie \u015bwiat\u0142a", "unit": "lx"},
    "bme280_temp": {"sensor": "BME280", "quantity": "Temperatura", "unit": "\u00b0C"},
    "bme280_press": {"sensor": "BME280", "quantity": "Ci\u015bnienie", "unit": "hPa"},
    "bme280_hum": {"sensor": "BME280", "quantity": "Wilgotno\u015b\u0107", "unit": "%"},
}


class NotMeasurementFrameError(Exception):
    pass


def safe_station_id(raw_station_id):
    allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-"
    sanitized = ""
    for ch in str(raw_station_id):
        if ch in allowed:
            sanitized += ch
    return sanitized[:16]


def is_uart_log_line(line):
    upper = str(line).strip().upper()
    for prefix in UART_LOG_PREFIXES:
        if upper.startswith(prefix):
            return True
    return False


def is_uart_control_line(line):
    upper = str(line).strip().upper()
    for prefix in UART_CONTROL_PREFIXES:
        if upper.startswith(prefix):
            return True
    return False


def build_measure_cmd(node=None):
    if node is None:
        return CMD_MEASURE
    return CMD_MEASURE + ":" + str(int(node))


def _extract_measurement_frame(line):
    raw = str(line).strip()
    if not raw:
        raise NotMeasurementFrameError("empty line")

    if is_uart_log_line(raw):
        raise NotMeasurementFrameError("uart log line")

    if is_uart_control_line(raw):
        raise NotMeasurementFrameError("uart control line")

    upper = raw.upper()
    if upper.startswith(UART_DATA_PREFIX):
        raw = raw[len(UART_DATA_PREFIX):].strip()

    if not raw:
        raise NotMeasurementFrameError("empty data frame")

    return raw


def _validate_iso_timestamp(timestamp):
    if not isinstance(timestamp, str) or len(timestamp) != 19:
        return False
    if timestamp[4] != "-" or timestamp[7] != "-" or timestamp[10] != "T":
        return False
    if timestamp[13] != ":" or timestamp[16] != ":":
        return False

    try:
        year = int(timestamp[0:4])
        month = int(timestamp[5:7])
        day = int(timestamp[8:10])
        hour = int(timestamp[11:13])
        minute = int(timestamp[14:16])
        second = int(timestamp[17:19])
    except ValueError:
        return False

    if year < 2000 or month < 1 or month > 12 or day < 1 or day > 31:
        return False
    if hour > 23 or minute > 59 or second > 59:
        return False
    return True


def _timestamp_to_epoch(timestamp):
    if not _validate_iso_timestamp(timestamp):
        return None

    try:
        import time
        year = int(timestamp[0:4])
        month = int(timestamp[5:7])
        day = int(timestamp[8:10])
        hour = int(timestamp[11:13])
        minute = int(timestamp[14:16])
        second = int(timestamp[17:19])
        return int(time.mktime((year, month, day, hour, minute, second, 0, 0)))
    except Exception:
        return None


def _parse_channel_pair(token):
    sep = token.find(":")
    if sep <= 0:
        raise ValueError("invalid channel token")

    channel_hex = token[:sep].strip()
    value_text = token[sep + 1:].strip()
    if not channel_hex or not value_text:
        raise ValueError("invalid channel token")

    channel_id = int(channel_hex, 16)
    field_name = CHANNEL_FIELDS.get(channel_id)
    if field_name is None:
        return None, None
    return field_name, value_text


def _empty_sensor_payload(timestamp, status):
    payload = {
        "timestamp": timestamp,
        "status": status,
    }
    for field_name in SENSOR_FIELDS:
        payload[field_name] = None
    return payload


def _parse_tagged_measurement_frame(frame):
    parts = frame.split(",")
    if len(parts) < 3:
        raise ValueError("tagged frame too short")

    timestamp = parts[0].strip()
    if not _validate_iso_timestamp(timestamp):
        raise ValueError("invalid ISO timestamp")

    station_id = safe_station_id(parts[1].strip())
    if not station_id:
        raise ValueError("invalid station_id")

    # Error-only frame from STM32:
    # DATA:<ts>,<station>,ERR:SI7021,BMP280,TSL2561
    # (no channel:value tokens; commas after ERR: are sensor names)
    first_token = parts[2].strip()
    if first_token.upper().startswith("ERR:"):
        status = ",".join(part.strip() for part in parts[2:] if part.strip())
        return station_id, _empty_sensor_payload(timestamp, status)

    if len(parts) < 4:
        raise ValueError("tagged frame too short")

    status = parts[-1].strip()
    payload = _empty_sensor_payload(timestamp, status)

    for token in parts[2:-1]:
        field_name, value_text = _parse_channel_pair(token.strip())
        if field_name is not None:
            payload[field_name] = value_text

    return station_id, payload


def _parse_legacy_measurement_frame(frame):
    if frame.count(",") != 12:
        raise NotMeasurementFrameError("not a measurement frame")

    parts = frame.split(",")
    if len(parts) != 13:
        raise ValueError("expected 13 CSV fields")

    try:
        year = int(parts[0].strip())
        month = int(parts[1].strip())
        day = int(parts[2].strip())
        hour = int(parts[3].strip())
        minute = int(parts[4].strip())
        second = int(parts[5].strip())
    except ValueError:
        raise ValueError("invalid date/time fields")

    timestamp = "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}".format(
        year, month, day, hour, minute, second
    )

    payload = {
        "timestamp": timestamp,
        "si7021_temp": parts[7].strip(),
        "si7021_hum": parts[8].strip(),
        "bmp280_temp": parts[9].strip(),
        "bmp280_press": parts[10].strip(),
        "tsl2561_lux": parts[11].strip(),
        "bme280_temp": None,
        "bme280_press": None,
        "bme280_hum": None,
        "status": parts[12].strip(),
    }

    station_id = safe_station_id(parts[6].strip())
    if not station_id:
        raise ValueError("invalid station_id")

    return station_id, payload


def parse_measurement_line(line):
    frame = _extract_measurement_frame(line)

    if "T" in frame[:20]:
        return _parse_tagged_measurement_frame(frame)

    return _parse_legacy_measurement_frame(frame)


def self_check():
    station_id, payload = parse_measurement_line(
        "DATA:2026-05-09T11:06:01,S0,01:23.45,02:65.20,04:1013.25,05:120.5,OK"
    )
    assert station_id == "S0"
    assert payload["si7021_temp"] == "23.45"
    assert payload["bmp280_press"] == "1013.25"
    assert payload["bmp280_temp"] is None
    assert payload["status"] == "OK"

    bme_station, bme_payload = parse_measurement_line(
        "DATA:2026-05-09T11:06:02,S2,06:22.80,07:1012.75,08:54.25,OK"
    )
    assert bme_station == "S2"
    assert bme_payload["bme280_temp"] == "22.80"
    assert bme_payload["bme280_press"] == "1012.75"
    assert bme_payload["bme280_hum"] == "54.25"

    legacy_station, legacy_payload = parse_measurement_line(
        "2026,05,09,11,06,01,S1,23.4,65.2,22.9,1013.20,120,OK"
    )
    assert legacy_station == "S1"
    assert legacy_payload["si7021_temp"] == "23.4"
    assert legacy_payload["tsl2561_lux"] == "120"

    err_station, err_payload = parse_measurement_line(
        "DATA:2026-05-30T19:34:39,S0,ERR:SI7021,BMP280,TSL2561"
    )
    assert err_station == "S0"
    assert err_payload["status"] == "ERR:SI7021,BMP280,TSL2561"
    assert err_payload["si7021_temp"] is None

    err2_station, err2_payload = parse_measurement_line(
        "DATA:2026-05-30T19:34:39,S1,ERR:BME280"
    )
    assert err2_station == "S1"
    assert err2_payload["status"] == "ERR:BME280"

    assert build_measure_cmd() == "CMD:MEASURE"
    assert build_measure_cmd(2) == "CMD:MEASURE:2"
    assert is_uart_control_line("ACK:PING")
    assert is_uart_control_line("ERR:BUSY")
    return True
