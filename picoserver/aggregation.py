"""Small, MicroPython-friendly helpers for weather data aggregation."""


def bucket_start(epoch, width_seconds):
    return (int(epoch) // int(width_seconds)) * int(width_seconds)


def new_bucket(start_epoch):
    return {
        "_bucket": int(start_epoch),
        "_sums": {},
        "_counts": {},
        "status": "OK",
    }


def _status_rank(status):
    value = str(status or "WARN").upper()
    if value.startswith("ERR"):
        return 2
    if value == "WARN":
        return 1
    return 0


def _merge_status(left, right):
    return right if _status_rank(right) > _status_rank(left) else left


def add_sample(bucket, values, fields, status="OK"):
    sums = bucket["_sums"]
    counts = bucket["_counts"]
    for field in fields:
        value = values.get(field)
        if value is None:
            continue
        sums[field] = sums.get(field, 0.0) + float(value)
        counts[field] = counts.get(field, 0) + 1
    bucket["status"] = _merge_status(bucket.get("status", "OK"), status)
    return bucket


def merge_record(bucket, record, fields):
    counts = record.get("_counts", {})
    values = {}
    for field in fields:
        value = record.get(field)
        count = int(counts.get(field, 0))
        if value is None or count < 1:
            continue
        values[field] = float(value) * count
        bucket["_counts"][field] = bucket["_counts"].get(field, 0) + count
    for field, total in values.items():
        bucket["_sums"][field] = bucket["_sums"].get(field, 0.0) + total
    bucket["status"] = _merge_status(
        bucket.get("status", "OK"), record.get("status", "WARN")
    )
    return bucket


def finalize_bucket(bucket, timestamp, fields):
    record = {
        "_bucket": bucket["_bucket"],
        "_counts": dict(bucket["_counts"]),
        "timestamp": timestamp,
        "status": bucket.get("status", "OK"),
    }
    for field in fields:
        count = bucket["_counts"].get(field, 0)
        record[field] = (
            round(bucket["_sums"].get(field, 0.0) / count, 3)
            if count
            else None
        )
    return record


def quantize(value, step):
    if value is None:
        return None
    step = float(step)
    scaled = float(value) / step
    units = int(scaled + 0.5) if scaled >= 0 else int(scaled - 0.5)
    result = units * step
    if step >= 1:
        return int(result)
    return round(result, 2)


def public_record(record, fields, field_steps):
    result = {
        "timestamp": record.get("timestamp", ""),
        "status": record.get("status", "WARN"),
    }
    for field in fields:
        result[field] = quantize(record.get(field), field_steps[field])
    return result


def average_recent(entries, quantity_fields):
    field_averages = {}
    for fields in quantity_fields.values():
        for field in fields:
            total = 0.0
            count = 0
            for entry in entries:
                value = entry.get(field)
                if value is not None:
                    total += float(value)
                    count += 1
            if count:
                field_averages[field] = total / count

    result = {}
    for quantity, fields in quantity_fields.items():
        values = [
            field_averages[field]
            for field in fields
            if field in field_averages
        ]
        result[quantity] = sum(values) / len(values) if values else None
    return result


def worst_status(entries):
    status = "OK"
    for entry in entries:
        status = _merge_status(status, entry.get("status", "WARN"))
    return status
