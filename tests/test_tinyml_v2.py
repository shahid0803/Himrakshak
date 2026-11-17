from __future__ import annotations

import csv
import json
from pathlib import Path
import unittest

from himrakshak.risk import assess_risk
from himrakshak.schema import SchemaValidationError, validate_reading
from himrakshak.tinyml import FEATURES, DEFAULT_MODEL_PATH, load_model, predict_reading
from himrakshak.v2_contract import V2_FEATURE_RANGES
from himrakshak.v2_simulator import V2_SCENARIOS, make_v2_packet

ROOT = Path(__file__).resolve().parents[1]


class TinyMLV2Tests(unittest.TestCase):
    def test_model_feature_contract_has_sixteen_ordered_features(self):
        self.assertEqual(len(FEATURES), 16)
        self.assertEqual(tuple(FEATURES), tuple(V2_FEATURE_RANGES))
        model = load_model()
        self.assertEqual(tuple(model["feature_names"]), FEATURES)

    def test_model_hash_is_verified_and_non_finite_features_are_rejected(self):
        model = load_model()
        values = [0.0] * len(FEATURES)
        with self.assertRaises(ValueError):
            from himrakshak.tinyml import predict_features
            predict_features([True] + values[1:], model)
        with self.assertRaises(ValueError):
            predict_features([float("nan")] + values[1:], model)

    def test_exported_model_artifacts_are_present_and_consistent(self):
        header = ROOT / "wokwi_esp32_demo" / "HimRakshakModel.h"
        metrics = ROOT / "models" / "training_metrics_v2.json"
        self.assertTrue(DEFAULT_MODEL_PATH.is_file())
        self.assertTrue(header.is_file())
        self.assertTrue(metrics.is_file())
        header_text = header.read_text(encoding="utf-8")
        metadata = json.loads(metrics.read_text(encoding="utf-8"))
        self.assertIn("namespace HimRakshakTinyML", header_text)
        self.assertIn("inline int predict(const float *x)", header_text)
        self.assertIn("MODEL_SHA256", header_text)
        self.assertEqual(metadata["training_source"], "synthetic_demonstration_scenarios_only")
        self.assertFalse(metadata["field_validated"])
        self.assertEqual(metadata["dataset_rows"], 2400)

    def test_dataset_has_all_features_and_balanced_synthetic_classes(self):
        path = ROOT / "data" / "processed" / "himrakshak_synthetic_v2.csv"
        with path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
        self.assertEqual(len(rows), 2400)
        self.assertEqual(tuple(rows[0].keys()), FEATURES + ("risk_class",))
        counts = {str(label): sum(int(row["risk_class"]) == label for row in rows) for label in (0, 1, 2)}
        self.assertEqual(counts, {"0": 800, "1": 800, "2": 800})

    def test_v2_scenarios_validate_and_return_expected_demo_states(self):
        expected = {
            "normal": "NORMAL",
            "heavy_rain": "WARNING",
            "slope_shift": "CRITICAL",
            "vibration_burst": "WARNING",
        }
        for scenario, expected_level in expected.items():
            packet = make_v2_packet("HR-01", 1, scenario)
            validated = validate_reading(packet)
            assessment = assess_risk(validated)
            self.assertEqual(assessment["risk_level"], expected_level)
            self.assertEqual(validated["sensor_schema_version"], 2)
            self.assertTrue(validated["is_simulated"])
            self.assertEqual(len([validated[name] for name in FEATURES]), 16)

    def test_v2_model_prediction_matches_runtime_for_scenarios(self):
        expected_classes = {
            "normal": 0,
            "heavy_rain": 1,
            "slope_shift": 2,
            "vibration_burst": 1,
        }
        for scenario, expected_class in expected_classes.items():
            packet = make_v2_packet("HR-01", 1, scenario)
            self.assertEqual(packet["tinyml_prediction"], predict_reading(packet))
            self.assertEqual(packet["tinyml_prediction"], expected_class, scenario)

    def test_v2_missing_feature_is_rejected(self):
        packet = make_v2_packet("HR-01", 1, "normal")
        del packet["groundwater_depth_m"]
        with self.assertRaises(SchemaValidationError):
            validate_reading(packet)

    def test_v2_boolean_feature_is_rejected_as_numeric(self):
        packet = make_v2_packet("HR-01", 1, "normal")
        packet["pore_water_pressure_kpa"] = True
        with self.assertRaises(SchemaValidationError):
            validate_reading(packet)

    def test_v2_legacy_fields_must_match_surface_moisture_and_tilt(self):
        packet = make_v2_packet("HR-01", 1, "normal")
        packet["soil_moisture_pct"] = 99
        with self.assertRaises(SchemaValidationError):
            validate_reading(packet)


    def test_v2_explicit_validity_and_provenance_fields_are_required(self):
        packet = make_v2_packet("HR-01", 1, "normal")
        del packet["rainfall_history_valid"]
        with self.assertRaises(SchemaValidationError):
            validate_reading(packet)
        packet = make_v2_packet("HR-01", 1, "normal")
        packet["model_ready"] = False
        packet["tinyml_prediction"] = 0
        with self.assertRaises(SchemaValidationError):
            validate_reading(packet)

    def test_v2_device_health_does_not_enter_tinyml_feature_vector(self):
        self.assertNotIn("battery_pct", FEATURES)
        self.assertNotIn("wifi_rssi_dbm", FEATURES)


if __name__ == "__main__":
    unittest.main()
