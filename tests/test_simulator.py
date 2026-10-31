import copy
import json
import subprocess
import sys
import unittest
from datetime import datetime, timezone

from himrakshak.risk import assess_risk
from himrakshak.schema import validate_reading
from himrakshak.simulator import JITTER_BOUNDS, SCENARIOS, SensorSimulator


class SimulatorTests(unittest.TestCase):
    def test_scenarios_have_expected_classifications_and_schema(self):
        expected = {
            "normal": "NORMAL",
            "heavy_rain": "CRITICAL",
            "slope_shift": "CRITICAL",
            "vibration_burst": "WARNING",
        }
        for scenario, level in expected.items():
            packet = SensorSimulator(scenario=scenario, seed=42).next_packet()
            self.assertEqual(assess_risk(packet)["risk_level"], level)
            self.assertEqual(validate_reading(packet), packet)
            self.assertEqual(packet["transport"], "PYTHON_SIMULATOR")

    def test_baselines_and_jitter_bounds(self):
        for scenario, baseline in SCENARIOS.items():
            packet = SensorSimulator(scenario=scenario, seed=42).next_packet()
            for field, value in baseline.items():
                low, high = JITTER_BOUNDS[field]
                self.assertGreaterEqual(packet[field], round(value + low, 1))
                self.assertLessEqual(packet[field], round(value + high, 1))
                self.assertEqual(round(packet[field], 1), packet[field])

    def test_packet_shape_and_utc_timestamp(self):
        packet = SensorSimulator(seed=42).next_packet()
        self.assertEqual(
            set(packet),
            {
                "node_id",
                "sequence",
                "timestamp",
                "soil_moisture_pct",
                "tilt_deg",
                "vibration_level",
                "battery_pct",
                "transport",
            },
        )
        self.assertTrue(packet["timestamp"].endswith("Z"))
        parsed = datetime.fromisoformat(packet["timestamp"].replace("Z", "+00:00"))
        self.assertEqual(parsed.tzinfo, timezone.utc)
        self.assertIsInstance(packet["sequence"], int)
        self.assertNotIsInstance(packet["sequence"], bool)
        self.assertEqual(validate_reading(packet), packet)

    def test_sequence_progression_and_reset(self):
        simulator = SensorSimulator(seed=42)
        self.assertEqual(simulator.sequence, 0)
        self.assertEqual(simulator.next_packet()["sequence"], 0)
        self.assertEqual(simulator.sequence, 1)
        simulator.next_packet()
        simulator.reset()
        self.assertEqual(simulator.sequence, 0)
        self.assertEqual(simulator.next_packet()["sequence"], 0)

    def test_same_seed_reproduces_readings_and_reset_replays_them(self):
        first = SensorSimulator(seed=42)
        second = SensorSimulator(seed=42)
        first_packet = first.next_packet()
        second_packet = second.next_packet()
        first_packet.pop("timestamp")
        second_packet.pop("timestamp")
        self.assertEqual(first_packet, second_packet)
        first.reset()
        replay = first.next_packet()
        replay.pop("timestamp")
        self.assertEqual(first_packet, replay)

    def test_same_seed_reproduces_sequences_and_assessments(self):
        first = SensorSimulator(scenario="heavy_rain", seed=42)
        second = SensorSimulator(scenario="heavy_rain", seed=42)
        for expected_sequence in (0, 1, 2):
            first_packet = first.next_packet()
            second_packet = second.next_packet()
            self.assertEqual(first_packet["sequence"], expected_sequence)
            self.assertEqual(second_packet["sequence"], expected_sequence)
            self.assertEqual(assess_risk(first_packet), assess_risk(second_packet))
            first_packet.pop("timestamp")
            second_packet.pop("timestamp")
            self.assertEqual(first_packet, second_packet)

    def test_different_seeds_change_sensor_readings(self):
        first = SensorSimulator(seed=42).next_packet()
        second = SensorSimulator(seed=43).next_packet()
        for packet in (first, second):
            packet.pop("timestamp")
        self.assertNotEqual(first, second)

    def test_invalid_constructor_arguments_are_rejected(self):
        with self.assertRaises(ValueError):
            SensorSimulator(scenario="unknown")
        with self.assertRaises(ValueError):
            SensorSimulator(node_id=" ")
        with self.assertRaises(ValueError):
            SensorSimulator(seed=True)

    def test_assessment_does_not_change_raw_packet(self):
        packet = SensorSimulator(scenario="slope_shift", seed=42).next_packet()
        original = copy.deepcopy(packet)
        result = assess_risk(packet)
        self.assertEqual(result["risk_level"], "CRITICAL")
        self.assertEqual(packet, original)

    def test_cli_outputs_json_lines_with_standard_shape(self):
        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "himrakshak.simulator",
                "--scenario",
                "heavy_rain",
                "--count",
                "3",
                "--seed",
                "42",
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(rows), 3)
        self.assertEqual([row["reading"]["sequence"] for row in rows], [0, 1, 2])
        for row in rows:
            self.assertEqual(set(row), {"reading", "assessment"})
            self.assertEqual(
                set(row["assessment"]),
                {"risk_level", "demo_score", "reasons", "model_id", "illustrative"},
            )
            self.assertEqual(row["assessment"]["risk_level"], "CRITICAL")

    def test_cli_rejects_non_positive_count_and_invalid_node(self):
        for count in ("0", "-1"):
            result = subprocess.run(
                [sys.executable, "-m", "himrakshak.simulator", "--count", count],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--count must be at least 1", result.stderr)
        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "himrakshak.simulator",
                "--node-id",
                " ",
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("node_id must be a non-empty string", result.stderr)


if __name__ == "__main__":
    unittest.main()
