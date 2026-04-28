import asyncio
import random
import time

STATIONS = ("S1", "S2", "S3")


def _iso_now():
    now = time.localtime()
    return "%04d-%02d-%02dT%02d:%02d:%02d" % (
        now[0], now[1], now[2], now[3], now[4], now[5]
    )


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
        return "ERR"
    if roll < 0.12:
        return "WARN"
    return "OK"


def _frame(timestamp, station_id, station_index):
    si7021_temp = _jitter(22.5 + station_index * 0.5, 1.8, 1)
    si7021_hum = _jitter(58.0 + station_index * 2.2, 5.5, 1)
    bmp280_temp = _jitter(22.0 + station_index * 0.5, 1.6, 1)
    bmp280_press = _jitter(1012.8 + station_index * 0.7, 2.2, 2)

    hour = int(timestamp[11:13])
    lux = _lux_for_hour(hour)
    status = _status_roll()

    return "%s,%s,%.1f,%.1f,%.1f,%.2f,%d,%s" % (
        timestamp,
        station_id,
        si7021_temp,
        si7021_hum,
        bmp280_temp,
        bmp280_press,
        lux,
        status,
    )


async def simulate_task(line_handler, interval_s=60):
    if interval_s < 1:
        interval_s = 1

    cycle = 0
    while True:
        timestamp = _iso_now()
        for index, station_id in enumerate(STATIONS):
            line_handler(_frame(timestamp, station_id, index))

        cycle += 1
        print("Simulacja: cykl", cycle, "timestamp:", timestamp)
        await asyncio.sleep(interval_s)
