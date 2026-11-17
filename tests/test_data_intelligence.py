from __future__ import annotations

from datetime import datetime, timedelta, timezone
import tempfile
import unittest

from himrakshak.data_intelligence.ml.datasets import training_status
from himrakshak.data_intelligence.processors.rainfall import accumulate_rainfall
from himrakshak.data_intelligence.quality import validate_coordinate, validate_observation
from himrakshak.data_intelligence.registry import PARAMETER_REGISTRY
from himrakshak.data_intelligence.storage import IntelligenceStore


class DataIntelligenceTests(unittest.TestCase):
    def test_registry_contains_catalogue_and_provenance_fields(self):
        self.assertGreaterEqual(len(PARAMETER_REGISTRY), 38)
        rainfall = PARAMETER_REGISTRY["rainfall_accumulation_24h"]
        self.assertTrue(rainfall.derived)
        self.assertEqual(rainfall.unit, "mm")
        self.assertIn("source", rainfall.as_dict())

    def test_rainfall_gap_is_not_treated_as_zero(self):
        start = datetime(2026, 1, 1, tzinfo=timezone.utc)
        rows = [
            {"observed_at": (start + timedelta(hours=index)).isoformat(), "value": 1.0}
            for index in range(24)
        ]
        rows.pop(12)
        self.assertIsNone(accumulate_rainfall(rows)[-1]["rainfall_24h"])

    def test_coordinate_and_observation_quality(self):
        self.assertEqual(validate_coordinate(75.1, 33.2).status, "valid")
        self.assertEqual(validate_coordinate(0, 0).status, "outside_study_area")
        observation = {
            "parameter_id": "air_temperature",
            "longitude": 75.1,
            "latitude": 33.2,
            "value": 12.0,
            "observed_at": "2026-01-01T00:00:00Z",
            "is_simulated": False,
        }
        self.assertEqual(validate_observation(observation).status, "valid")

    def test_simulated_rows_cannot_make_model_eligible(self):
        rows = [{"target": 0, "is_simulated": True} for _ in range(40)]
        status = training_status(rows, model_type="early_warning")
        self.assertEqual(status["status"], "NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS")

    def test_sqlite_store_deduplicates_observations(self):
        with tempfile.TemporaryDirectory() as directory:
            store = IntelligenceStore(f"{directory}\\intelligence.sqlite3")
            row = {
                "observation_id": "obs-1", "station_id": None, "longitude": 75.1,
                "latitude": 33.2, "parameter_id": "air_temperature", "value": 12.0,
                "unit": "deg C", "source": "Open-Meteo", "source_product": "test",
                "observed_at": "2026-01-01T00:00:00Z", "retrieved_at": "2026-01-01T01:00:00Z",
                "quality_status": "valid", "spatial_resolution": None, "uncertainty": None,
                "processing_version": "18.1", "is_simulated": 0,
            }
            self.assertEqual(store.insert_observations([row, row]), 2)
            self.assertEqual(len(store.latest()), 1)


if __name__ == "__main__":
    unittest.main()
