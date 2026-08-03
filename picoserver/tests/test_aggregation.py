import os
import sys
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

import aggregation


FIELDS = ("temp_a", "temp_b", "humidity")


class AggregationTests(unittest.TestCase):
    def test_bucket_boundaries(self):
        self.assertEqual(aggregation.bucket_start(599, 600), 0)
        self.assertEqual(aggregation.bucket_start(600, 600), 600)
        self.assertEqual(aggregation.bucket_start(3599, 3600), 0)
        self.assertEqual(aggregation.bucket_start(3600, 3600), 3600)

    def test_missing_values_and_worst_status(self):
        bucket = aggregation.new_bucket(0)
        aggregation.add_sample(
            bucket,
            {"temp_a": 20.0, "temp_b": None, "humidity": 50},
            FIELDS,
            "OK",
        )
        aggregation.add_sample(
            bucket,
            {"temp_a": 22.0, "temp_b": 24.0, "humidity": None},
            FIELDS,
            "WARN",
        )
        record = aggregation.finalize_bucket(
            bucket, "2026-01-01T00:00:00", FIELDS
        )

        self.assertEqual(record["temp_a"], 21.0)
        self.assertEqual(record["temp_b"], 24.0)
        self.assertEqual(record["humidity"], 50.0)
        self.assertEqual(record["_counts"]["temp_a"], 2)
        self.assertEqual(record["status"], "WARN")

    def test_hourly_merge_is_weighted_by_sample_count(self):
        first = {
            "temp_a": 10.0,
            "temp_b": None,
            "humidity": None,
            "_counts": {"temp_a": 1},
            "status": "OK",
        }
        second = {
            "temp_a": 20.0,
            "temp_b": None,
            "humidity": None,
            "_counts": {"temp_a": 3},
            "status": "OK",
        }
        bucket = aggregation.new_bucket(0)
        aggregation.merge_record(bucket, first, FIELDS)
        aggregation.merge_record(bucket, second, FIELDS)
        record = aggregation.finalize_bucket(
            bucket, "2026-01-01T00:00:00", FIELDS
        )

        self.assertEqual(record["temp_a"], 17.5)
        self.assertEqual(record["_counts"]["temp_a"], 4)

    def test_six_ten_minute_records_form_one_hour(self):
        hour = aggregation.new_bucket(0)
        for index in range(6):
            ten_minutes = aggregation.new_bucket(index * 600)
            aggregation.add_sample(
                ten_minutes, {"temp_a": 20.0 + index}, FIELDS
            )
            record = aggregation.finalize_bucket(
                ten_minutes,
                "2026-01-01T00:{:02d}:00".format(index * 10),
                FIELDS,
            )
            aggregation.merge_record(hour, record, FIELDS)

        record = aggregation.finalize_bucket(
            hour, "2026-01-01T00:00:00", FIELDS
        )
        self.assertEqual(record["temp_a"], 22.5)
        self.assertEqual(record["_counts"]["temp_a"], 6)

    def test_quantization(self):
        self.assertEqual(aggregation.quantize(21.26, 0.5), 21.5)
        self.assertEqual(aggregation.quantize(21.24, 0.5), 21.0)
        self.assertEqual(aggregation.quantize(-1.25, 0.5), -1.5)
        self.assertEqual(aggregation.quantize(53.6, 1), 54)

    def test_two_stage_recent_average(self):
        entries = [
            {"temp_a": 20.0, "temp_b": 24.0, "humidity": 40.0},
            {"temp_a": 22.0, "temp_b": 26.0, "humidity": 44.0},
        ]
        result = aggregation.average_recent(
            entries,
            {
                "temperature": ("temp_a", "temp_b"),
                "humidity": ("humidity",),
            },
        )

        self.assertEqual(result["temperature"], 23.0)
        self.assertEqual(result["humidity"], 42.0)

    def test_public_record_strips_internal_counts(self):
        record = {
            "timestamp": "2026-01-01T00:00:00",
            "status": "OK",
            "temp_a": 21.26,
            "temp_b": None,
            "humidity": 53.6,
            "_counts": {"temp_a": 10},
        }
        result = aggregation.public_record(
            record,
            FIELDS,
            {"temp_a": 0.5, "temp_b": 0.5, "humidity": 1},
        )
        self.assertNotIn("_counts", result)
        self.assertEqual(result["temp_a"], 21.5)
        self.assertEqual(result["humidity"], 54)

    def test_stations_are_aggregated_independently(self):
        buckets = {}
        for station, value in (("inside", 20.0), ("outside", 5.0)):
            buckets[station] = aggregation.new_bucket(0)
            aggregation.add_sample(
                buckets[station], {"temp_a": value}, FIELDS
            )

        inside = aggregation.finalize_bucket(
            buckets["inside"], "2026-01-01T00:00:00", FIELDS
        )
        outside = aggregation.finalize_bucket(
            buckets["outside"], "2026-01-01T00:00:00", FIELDS
        )
        self.assertEqual(inside["temp_a"], 20.0)
        self.assertEqual(outside["temp_a"], 5.0)

    def test_virtual_24h_three_station_simulation(self):
        for station_index in range(3):
            ten_minute_records = []
            for bucket_index in range(144):
                bucket = aggregation.new_bucket(bucket_index * 600)
                for minute in range(10):
                    aggregation.add_sample(
                        bucket,
                        {"temp_a": 10.0 + station_index + minute / 10},
                        FIELDS,
                    )
                ten_minute_records.append(
                    aggregation.finalize_bucket(
                        bucket, "2026-01-01T00:00:00", FIELDS
                    )
                )

            hourly_records = []
            for hour_index in range(24):
                bucket = aggregation.new_bucket(hour_index * 3600)
                for record in ten_minute_records[hour_index * 6:(hour_index + 1) * 6]:
                    aggregation.merge_record(bucket, record, FIELDS)
                hourly_records.append(
                    aggregation.finalize_bucket(
                        bucket, "2026-01-01T00:00:00", FIELDS
                    )
                )

            self.assertEqual(len(ten_minute_records), 144)
            self.assertEqual(len(hourly_records), 24)
            self.assertEqual(hourly_records[0]["_counts"]["temp_a"], 60)


if __name__ == "__main__":
    unittest.main()
