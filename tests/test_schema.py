import math
import unittest
from collections import UserDict

from himrakshak.schema import SchemaValidationError, validate_reading


NORMAL = {
    "node_id": " HR-01 ",
    "sequence": 1,
    "timestamp": "2026-10-27T12:30:00Z",
    "soil_moisture_pct": 68,
    "tilt_deg": 4.2,
    "vibration_level": 35,
    "battery_pct": 82,
}

WOKWI = {
    "node_id": "HR-01",
    "sequence": 2,
    "timestamp": "SIMULATED",
    "soil_moisture_pct": 70,
    "tilt_deg": 5.5,
    "vibration_level": 85,
    "battery_pct": 82,
    "transport": "WOKWI_SOFTWARE_SIMULATION",
}


class SchemaTests(unittest.TestCase):
    def assertInvalid(self, payload, field):
        with self.assertRaisesRegex(SchemaValidationError, field):
            validate_reading(payload)

    def test_valid_normal_telemetry_is_normalized(self):
        result = validate_reading(NORMAL)
        self.assertEqual(result["node_id"], "HR-01")
        self.assertEqual(result["soil_moisture_pct"], 68)
        self.assertEqual(result["timestamp"], "2026-10-27T12:30:00Z")

    def test_timezone_offset_is_normalized_to_utc(self):
        payload = dict(NORMAL)
        payload["timestamp"] = "2026-10-27T14:30:00+02:00"
        self.assertEqual(
            validate_reading(payload)["timestamp"],
            "2026-10-27T12:30:00Z",
        )

    def test_valid_wokwi_packet_accepts_simulated_timestamp(self):
        self.assertEqual(validate_reading(WOKWI)["timestamp"], "SIMULATED")

    def test_missing_required_field_is_rejected(self):
        payload = dict(NORMAL)
        del payload["battery_pct"]
        self.assertInvalid(payload, "battery_pct")

    def test_non_dictionary_payload_is_rejected(self):
        self.assertInvalid(["not", "a", "packet"], "payload")

    def test_empty_node_id_is_rejected(self):
        payload = dict(NORMAL)
        payload["node_id"] = "  "
        self.assertInvalid(payload, "node_id")

    def test_excessively_long_node_id_is_rejected(self):
        payload = dict(NORMAL)
        payload["node_id"] = "x" * 41
        self.assertInvalid(payload, "node_id")

    def test_invalid_sequence_is_rejected(self):
        for value in (-1, True, 1.5, "1"):
            payload = dict(NORMAL)
            payload["sequence"] = value
            self.assertInvalid(payload, "sequence")

    def test_zero_sequence_is_accepted(self):
        payload = dict(NORMAL)
        payload["sequence"] = 0
        self.assertEqual(validate_reading(payload)["sequence"], 0)

    def test_boolean_sensor_number_is_rejected(self):
        payload = dict(NORMAL)
        payload["soil_moisture_pct"] = True
        self.assertInvalid(payload, "soil_moisture_pct")

    def test_non_numeric_sensor_reading_is_rejected(self):
        payload = dict(NORMAL)
        payload["tilt_deg"] = "4.2"
        self.assertInvalid(payload, "tilt_deg")

    def test_nan_and_infinity_are_rejected(self):
        for value in (math.nan, math.inf, -math.inf):
            payload = dict(NORMAL)
            payload["vibration_level"] = value
            self.assertInvalid(payload, "vibration_level")

    def test_moisture_range_is_enforced(self):
        for value in (-0.1, 100.1):
            payload = dict(NORMAL)
            payload["soil_moisture_pct"] = value
            self.assertInvalid(payload, "soil_moisture_pct")

    def test_tilt_range_is_enforced(self):
        for value in (-0.1, 15.1):
            payload = dict(NORMAL)
            payload["tilt_deg"] = value
            self.assertInvalid(payload, "tilt_deg")

    def test_vibration_range_is_enforced(self):
        for value in (-1, 101):
            payload = dict(NORMAL)
            payload["vibration_level"] = value
            self.assertInvalid(payload, "vibration_level")

    def test_battery_range_is_enforced(self):
        for value in (-1, 101):
            payload = dict(NORMAL)
            payload["battery_pct"] = value
            self.assertInvalid(payload, "battery_pct")

    def test_malformed_timestamp_is_rejected(self):
        payload = dict(NORMAL)
        payload["timestamp"] = "not-a-timestamp"
        self.assertInvalid(payload, "timestamp")

    def test_timezone_naive_timestamp_is_rejected(self):
        payload = dict(NORMAL)
        payload["timestamp"] = "2026-10-27T12:30:00"
        self.assertInvalid(payload, "timestamp")

    def test_simulated_timestamp_requires_wokwi_transport(self):
        payload = dict(WOKWI)
        payload["transport"] = "SIMULATED_LORA"
        self.assertInvalid(payload, "timestamp")

    def test_simulated_timestamp_without_transport_is_rejected(self):
        payload = dict(WOKWI)
        del payload["transport"]
        self.assertInvalid(payload, "timestamp")

    def test_derived_metadata_is_retained_but_not_assessed(self):
        payload = dict(NORMAL)
        payload.update(
            {
                "risk": "CRITICAL",
                "risk_score_demo": 99,
                "reasons": ["untrusted metadata"],
            }
        )
        result = validate_reading(payload)
        self.assertEqual(result["risk"], "CRITICAL")
        self.assertEqual(result["risk_score_demo"], 99)

    def test_missing_readings_are_not_defaulted_to_safe_values(self):
        payload = dict(NORMAL)
        del payload["tilt_deg"]
        with self.assertRaises(SchemaValidationError):
            validate_reading(payload)

    def test_each_required_field_is_rejected_when_missing(self):
        for field in (
            "node_id",
            "sequence",
            "timestamp",
            "soil_moisture_pct",
            "tilt_deg",
            "vibration_level",
            "battery_pct",
        ):
            with self.subTest(field=field):
                payload = dict(NORMAL)
                del payload[field]
                self.assertInvalid(payload, field)

    def test_node_id_exact_limit_is_accepted(self):
        payload = dict(NORMAL)
        payload["node_id"] = "x" * 40
        self.assertEqual(len(validate_reading(payload)["node_id"]), 40)

    def test_node_id_non_string_is_rejected(self):
        payload = dict(NORMAL)
        payload["node_id"] = 123
        self.assertInvalid(payload, "node_id")

    def test_sequence_large_non_negative_integer_is_accepted(self):
        payload = dict(NORMAL)
        payload["sequence"] = 10**100
        self.assertEqual(validate_reading(payload)["sequence"], 10**100)

    def test_numeric_boundaries_are_accepted_for_each_sensor(self):
        ranges = {
            "soil_moisture_pct": (0, 100),
            "tilt_deg": (0, 15),
            "vibration_level": (0, 100),
            "battery_pct": (0, 100),
        }
        for field, (minimum, maximum) in ranges.items():
            for value in (minimum, maximum):
                with self.subTest(field=field, value=value):
                    payload = dict(NORMAL)
                    payload[field] = value
                    self.assertEqual(validate_reading(payload)[field], value)

    def test_each_numeric_sensor_rejects_invalid_types(self):
        invalid_values = (None, "1", object(), True, False)
        for field in (
            "soil_moisture_pct",
            "tilt_deg",
            "vibration_level",
            "battery_pct",
        ):
            for value in invalid_values:
                with self.subTest(field=field, value=repr(value)):
                    payload = dict(NORMAL)
                    payload[field] = value
                    self.assertInvalid(payload, field)

    def test_each_numeric_sensor_rejects_non_finite_values(self):
        for field in (
            "soil_moisture_pct",
            "tilt_deg",
            "vibration_level",
            "battery_pct",
        ):
            for value in (math.nan, math.inf, -math.inf):
                with self.subTest(field=field, value=value):
                    payload = dict(NORMAL)
                    payload[field] = value
                    self.assertInvalid(payload, field)

    def test_each_numeric_sensor_rejects_values_outside_its_range(self):
        ranges = {
            "soil_moisture_pct": (-0.1, 100.1),
            "tilt_deg": (-0.1, 15.1),
            "vibration_level": (-0.1, 100.1),
            "battery_pct": (-0.1, 100.1),
        }
        for field, invalid_values in ranges.items():
            for value in invalid_values:
                with self.subTest(field=field, value=value):
                    payload = dict(NORMAL)
                    payload[field] = value
                    self.assertInvalid(payload, field)

    def test_timestamp_with_z_and_non_hour_offset_is_normalized(self):
        payload = dict(NORMAL)
        payload["timestamp"] = "2026-10-27T18:00:00-05:30"
        self.assertEqual(
            validate_reading(payload)["timestamp"],
            "2026-10-27T23:30:00Z",
        )

    def test_invalid_timestamp_types_and_empty_values_are_rejected(self):
        for value in (None, 123, "", "2026-99-99T12:00:00Z", "2026-10-27"):
            with self.subTest(value=repr(value)):
                payload = dict(NORMAL)
                payload["timestamp"] = value
                self.assertInvalid(payload, "timestamp")

    def test_simulated_timestamp_requires_exact_wokwi_transport(self):
        for transport in (None, "", "WOKWI_SOFTWARE_SIMULATION ", 123, "SIMULATED_LORA"):
            with self.subTest(transport=repr(transport)):
                payload = dict(WOKWI)
                if transport is None:
                    del payload["transport"]
                else:
                    payload["transport"] = transport
                self.assertInvalid(payload, "timestamp")

    def test_transport_is_optional_but_invalid_supplied_values_are_rejected(self):
        payload = dict(NORMAL)
        self.assertNotIn("transport", validate_reading(payload))
        for value in ("", "   ", 123, True, False):
            with self.subTest(value=repr(value)):
                invalid = dict(NORMAL)
                invalid["transport"] = value
                self.assertInvalid(invalid, "transport")

    def test_valid_transport_values_are_retained(self):
        for transport in ("SIMULATED_LORA", "WOKWI_SOFTWARE_SIMULATION"):
            with self.subTest(transport=transport):
                payload = dict(NORMAL)
                payload["transport"] = transport
                self.assertEqual(validate_reading(payload)["transport"], transport)

    def test_mapping_inputs_are_accepted_and_return_a_plain_copy(self):
        payload = UserDict(NORMAL)
        result = validate_reading(payload)
        self.assertIsInstance(result, dict)
        self.assertIsNot(result, payload)
        self.assertEqual(result["node_id"], "HR-01")

    def test_normalization_does_not_mutate_input(self):
        payload = dict(NORMAL)
        original = dict(payload)
        validate_reading(payload)
        self.assertEqual(payload, original)

    def test_unknown_and_derived_metadata_are_retained(self):
        payload = dict(NORMAL)
        payload.update(
            {
                "risk": "CRITICAL",
                "risk_score_demo": 99,
                "reasons": ["untrusted"],
                "future_field": {"source": "test"},
            }
        )
        result = validate_reading(payload)
        self.assertEqual(result["risk"], "CRITICAL")
        self.assertEqual(result["risk_score_demo"], 99)
        self.assertEqual(result["reasons"], ["untrusted"])
        self.assertEqual(result["future_field"], {"source": "test"})

    def test_each_legacy_alias_can_supply_its_canonical_field(self):
        pairs = {
            "node": ("node_id", "HR-01"),
            "moisture": ("soil_moisture_pct", 68),
            "tilt": ("tilt_deg", 4.2),
            "vibration": ("vibration_level", 35),
        }
        for alias, (canonical, value) in pairs.items():
            with self.subTest(alias=alias):
                payload = dict(NORMAL)
                del payload[canonical]
                payload[alias] = value
                result = validate_reading(payload)
                self.assertEqual(result[canonical], value)
                self.assertNotIn(alias, result)

    def test_all_legacy_aliases_can_supply_one_packet(self):
        payload = dict(NORMAL)
        for canonical, alias in (
            ("node_id", "node"),
            ("soil_moisture_pct", "moisture"),
            ("tilt_deg", "tilt"),
            ("vibration_level", "vibration"),
        ):
            payload[alias] = payload.pop(canonical)
        result = validate_reading(payload)
        self.assertEqual(result["node_id"], "HR-01")
        self.assertEqual(result["soil_moisture_pct"], 68)
        self.assertEqual(result["tilt_deg"], 4.2)
        self.assertEqual(result["vibration_level"], 35)
        for alias in ("node", "moisture", "tilt", "vibration"):
            self.assertNotIn(alias, result)

    def test_alias_values_at_boundaries_are_accepted(self):
        cases = (
            ("moisture", "soil_moisture_pct", 0, 100),
            ("tilt", "tilt_deg", 0, 15),
            ("vibration", "vibration_level", 0, 100),
        )
        for alias, canonical, minimum, maximum in cases:
            for value in (minimum, maximum):
                with self.subTest(alias=alias, value=value):
                    payload = dict(NORMAL)
                    del payload[canonical]
                    payload[alias] = value
                    self.assertEqual(validate_reading(payload)[canonical], value)

    def test_matching_canonical_and_alias_values_are_accepted(self):
        payload = dict(NORMAL)
        payload.update(
            {
                "node": " HR-01 ",
                "moisture": 68.0,
                "tilt": 4.2,
                "vibration": 35.0,
            }
        )
        result = validate_reading(payload)
        self.assertEqual(result["node_id"], "HR-01")
        self.assertEqual(result["soil_moisture_pct"], 68)
        self.assertEqual(result["tilt_deg"], 4.2)
        self.assertEqual(result["vibration_level"], 35)
        for alias in ("node", "moisture", "tilt", "vibration"):
            self.assertNotIn(alias, result)

    def test_conflicting_canonical_and_alias_values_are_rejected(self):
        pairs = (
            ("node_id", "node", "HR-01", "HR-02"),
            ("soil_moisture_pct", "moisture", 68, 69),
            ("tilt_deg", "tilt", 4.2, 4.3),
            ("vibration_level", "vibration", 35, 36),
        )
        for canonical, alias, canonical_value, alias_value in pairs:
            with self.subTest(canonical=canonical):
                payload = dict(NORMAL)
                payload[canonical] = canonical_value
                payload[alias] = alias_value
                self.assertInvalid(payload, canonical)

    def test_alias_numeric_comparison_does_not_coerce_strings(self):
        payload = dict(NORMAL)
        payload["moisture"] = "68"
        with self.assertRaisesRegex(SchemaValidationError, "moisture"):
            validate_reading(payload)

    def test_alias_values_reuse_canonical_validation_rules(self):
        numeric_invalid_values = (
            True,
            False,
            "68",
            None,
            math.nan,
            math.inf,
            -math.inf,
            -1,
            101,
        )
        for alias, canonical in (
            ("moisture", "soil_moisture_pct"),
            ("tilt", "tilt_deg"),
            ("vibration", "vibration_level"),
        ):
            for value in numeric_invalid_values:
                with self.subTest(alias=alias, value=repr(value)):
                    payload = dict(NORMAL)
                    del payload[canonical]
                    payload[alias] = value
                    self.assertInvalid(payload, alias)

    def test_node_alias_reuses_canonical_string_validation(self):
        for value in (None, 123, "", " " * 3, "x" * 41):
            with self.subTest(value=repr(value)):
                payload = dict(NORMAL)
                del payload["node_id"]
                payload["node"] = value
                self.assertInvalid(payload, "node_id")

    def test_legacy_aliases_preserve_unrelated_metadata_and_input(self):
        payload = dict(NORMAL)
        payload.pop("node_id")
        payload["node"] = "HR-01"
        payload["extra"] = {"source": "legacy"}
        original = dict(payload)
        result = validate_reading(payload)
        self.assertEqual(result["extra"], {"source": "legacy"})
        self.assertEqual(payload, original)

    def test_missing_canonical_and_alias_is_rejected(self):
        payload = dict(NORMAL)
        del payload["soil_moisture_pct"]
        self.assertInvalid(payload, "soil_moisture_pct")

    def test_exception_names_the_invalid_field_and_is_value_error(self):
        payload = dict(NORMAL)
        payload["battery_pct"] = 101
        with self.assertRaises(SchemaValidationError) as context:
            validate_reading(payload)
        self.assertIsInstance(context.exception, ValueError)
        self.assertIn("battery_pct", str(context.exception))


if __name__ == "__main__":
    unittest.main()
