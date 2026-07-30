import asyncio
import random
import time

STATIONS = ("S0", "S1", "S2")
ERROR_SENSORS = ("BMP280", "SI7021", "TSL2561")


def _jitter(base, spread, digits):
    value = base + (random.random() * 2.0 - 1.0) * spread
    return round(value, digits)


def _lux_for_hour(hour):
    if 6 <= hour <= 18:
        peak = 900 - abs(12 - hour) * 120
        return int(max(80, peak + random.randint(-70, 70)))
    return random.randint(0, 30)


def _status_roll():
    roll = random.random()
    if roll < 0.03:
        sensor_index = random.randint(0, len(ERROR_SENSORS) - 1)
        return "ERR:" + ERROR_SENSORS[sensor_index]
    if roll < 0.12:
        return "WARN"
    return "OK"


def _frame(now, station_id, station_index):
    timestamp = "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}".format(
        now[0], now[1], now[2], now[3], now[4], now[5]
    )
    si7021_temp = _jitter(22.5 + station_index * 0.5, 1.8, 2)
    si7021_hum = _jitter(58.0 + station_index * 2.2, 5.5, 2)
    bmp280_temp = _jitter(22.0 + station_index * 0.5, 1.6, 2)
    bmp280_press = _jitter(1012.8 + station_index * 0.7, 2.2, 2)
    lux = _lux_for_hour(now[3])
    status = _status_roll()

    channels = (
        "01:{:.2f},02:{:.2f},03:{:.2f},04:{:.2f},05:{}".format(
            si7021_temp,
            si7021_hum,
            bmp280_temp,
            bmp280_press,
            lux,
        )
    )

    return "DATA:{},{},{},{}".format(timestamp, station_id, channels, status)


async def simulate_task(line_handler, interval_s=20):
    if interval_s < 1:
        interval_s = 1

    cycle = 0
    while True:
        now = time.localtime()
        for index, station_id in enumerate(STATIONS):
            line_handler(_frame(now, station_id, index))

        cycle += 1
        timestamp = "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}".format(
            now[0], now[1], now[2], now[3], now[4], now[5]
        )
        print("Simulacja: cykl", cycle, "timestamp:", timestamp)
        await asyncio.sleep(interval_s)
