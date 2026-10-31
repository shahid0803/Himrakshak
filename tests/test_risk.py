import copy
import unittest

from himrakshak.risk import MODEL_ID, assess_risk
from himrakshak.schema import SchemaValidationError


def reading(moisture=32.0, tilt=1.2, vibration=12.0):
    return {
        "node_id": "HR-01",
        "sequence": 0,
        "timestamp": "2026-10-27T12:30:00Z",
        "soil_moisture_pct": moisture,
        "tilt_deg": tilt,
        "vibration_level": vibration,
        "battery_pct": 82.0,
    }


class RiskTests(unittest.TestCase):
    def test_public_result_shape_and_types(self):
        result = assess_risk(reading())
        self.assertEqual(
            set(result),
            {"risk_level", "demo_score", "reasons", "model_id", "illustrative"},
        )
        self.assertEqual(result["risk_level"], "NORMAL")
        self.assertEqual(result["demo_score"], 0)
        self.assertIsInstance(result["demo_score"], int)
        self.assertEqual(result["reasons"], [])
        self.assertEqual(result["model_id"], MODEL_ID)
        self.assertEqual(MODEL_ID, "himrakshak-demo-risk-engine-v1")
        self.assertTrue(result["illustrative"])

    def test_warning_threshold_boundaries(self):
        self.assertEqual(assess_risk(reading(moisture=60))["risk_level"], "WARNING")
        self.assertEqual(assess_risk(reading(tilt=4))["risk_level"], "WARNING")
        self.assertEqual(
            assess_risk(reading(vibration=65))["risk_level"], "WARNING"
        )
        result = assess_risk(reading(moisture=70, tilt=2, vibration=100))
        self.assertEqual(result["risk_level"], "WARNING")
        self.assertIn("Demonstration score reached the warning threshold", result["reasons"])

    def test_each_critical_condition_is_applied(self):
        self.assertEqual(assess_risk(reading(tilt=8))["risk_level"], "CRITICAL")
        self.assertEqual(
            assess_risk(reading(moisture=75, tilt=4))["risk_level"], "CRITICAL"
        )
        self.assertEqual(
            assess_risk(reading(moisture=100, tilt=7, vibration=100))["risk_level"],
            "CRITICAL",
        )

    def test_critical_reasons_are_exact_and_ordered(self):
        result = assess_risk(reading(moisture=75, tilt=8, vibration=65))
        self.assertEqual(
            result["reasons"],
            [
                "Elevated simulated moisture",
                "Elevated simulated tilt",
                "Elevated simulated vibration",
                "Demonstration score reached the warning threshold",
                "Tilt reached the critical demonstration threshold",
                "Combined moisture and tilt met the critical demonstration rule",
                "Demonstration score reached the critical threshold",
            ],
        )

    def test_heavy_rain_includes_all_applicable_reasons_in_order(self):
        result = assess_risk(reading(moisture=78.4, tilt=4.2, vibration=29.1))
        self.assertEqual(result["risk_level"], "CRITICAL")
        self.assertEqual(
            result["reasons"],
            [
                "Elevated simulated moisture",
                "Elevated simulated tilt",
                "Demonstration score reached the warning threshold",
                "Combined moisture and tilt met the critical demonstration rule",
            ],
        )

    def test_slope_shift_includes_all_applicable_reasons_in_order(self):
        result = assess_risk(reading(moisture=70.4, tilt=8.7, vibration=54.1))
        self.assertEqual(result["risk_level"], "CRITICAL")
        self.assertEqual(
            result["reasons"],
            [
                "Elevated simulated moisture",
                "Elevated simulated tilt",
                "Demonstration score reached the warning threshold",
                "Tilt reached the critical demonstration threshold",
            ],
        )

    def test_score_rounding_and_clamping(self):
        self.assertEqual(
            assess_risk(reading(moisture=100, tilt=15, vibration=100))["demo_score"],
            100,
        )
        self.assertEqual(assess_risk(reading())["demo_score"], 0)
        self.assertEqual(
            assess_risk(reading(moisture=45, tilt=2, vibration=25))["demo_score"],
            0,
        )

    def test_forged_metadata_is_ignored_and_input_is_not_modified(self):
        packet = reading(moisture=70, tilt=5, vibration=70)
        packet.update({"risk": "NORMAL", "risk_score_demo": 0, "reasons": []})
        original = copy.deepcopy(packet)
        result = assess_risk(packet)
        self.assertEqual(result["risk_level"], "WARNING")
        self.assertEqual(packet, original)

    def test_incomplete_or_invalid_readings_raise_schema_error(self):
        packet = reading()
        del packet["tilt_deg"]
        with self.assertRaises(SchemaValidationError):
            assess_risk(packet)
        packet = reading()
        packet["soil_moisture_pct"] = True
        with self.assertRaises(SchemaValidationError):
            assess_risk(packet)

    def test_alias_only_packet_matches_equivalent_canonical_assessment(self):
        canonical = reading(moisture=70, tilt=5, vibration=70)
        aliased = dict(canonical)
        aliased["node"] = aliased.pop("node_id")
        aliased["moisture"] = aliased.pop("soil_moisture_pct")
        aliased["tilt"] = aliased.pop("tilt_deg")
        aliased["vibration"] = aliased.pop("vibration_level")
        self.assertEqual(assess_risk(aliased), assess_risk(canonical))
        packet = reading()
        packet["vibration_level"] = True
        with self.assertRaises(SchemaValidationError):
            assess_risk(packet)


if __name__ == "__main__":
    unittest.main()
