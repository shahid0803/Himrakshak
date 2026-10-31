import copy
import unittest

from himrakshak.stations import (
    MAX_EVENTS,
    STATION_ROSTER,
    StationManager,
    StationSequenceConflictError,
)


class StationManagerTests(unittest.TestCase):
    def packet(self, node_id):
        return {
            "node_id": node_id,
            "sequence": 99,
            "timestamp": "2026-10-28T03:30:00Z",
            "soil_moisture_pct": 32.4,
            "tilt_deg": 0.9,
            "vibration_level": 11.1,
            "battery_pct": 82.0,
            "transport": "PYTHON_SIMULATOR",
        }

    def test_exact_roster_and_independent_instances(self):
        manager = StationManager(seed=42)
        self.assertEqual(STATION_ROSTER, (
            ("HR-01", "Virtual Station 1"),
            ("HR-02", "Virtual Station 2"),
            ("HR-03", "Virtual Station 3"),
        ))
        self.assertEqual(set(manager.stations), {"HR-01", "HR-02", "HR-03"})
        self.assertEqual(len({id(s.simulator) for s in manager.stations.values()}), 3)

    def test_scenarios_sequences_and_isolation(self):
        manager = StationManager(seed=42)
        first = manager.simulate("HR-01", "heavy_rain")
        second = manager.simulate("HR-02", "normal")
        self.assertEqual(first["reading"]["sequence"], 0)
        self.assertEqual(second["reading"]["sequence"], 0)
        self.assertEqual(manager.station_details("HR-01")["scenario"], "heavy_rain")
        self.assertEqual(manager.station_details("HR-02")["scenario"], "normal")
        self.assertIsNone(manager.station_details("HR-03")["latest"])
        self.assertEqual(manager.station_details("HR-03")["event_count"], 0)
        next_first = manager.simulate("HR-01", "slope_shift")
        self.assertEqual(next_first["reading"]["sequence"], 1)

    def test_seeded_runs_are_reproducible_and_reset_is_available(self):
        first = StationManager(seed=42)
        second = StationManager(seed=42)
        for node_id, scenario in (("HR-01", "heavy_rain"), ("HR-02", "slope_shift")):
            a = first.simulate(node_id, scenario)["reading"]
            b = second.simulate(node_id, scenario)["reading"]
            for field in ("soil_moisture_pct", "tilt_deg", "vibration_level", "battery_pct"):
                self.assertEqual(a[field], b[field])
        simulator = first.get("HR-01").simulator
        simulator.next_packet()
        simulator.reset()
        self.assertEqual(simulator.sequence, 0)

    def test_ingest_preserves_device_sequence_and_rejects_duplicates(self):
        manager = StationManager(seed=42)
        payload = self.packet("HR-02")
        original = copy.deepcopy(payload)
        result = manager.ingest("HR-02", payload)
        self.assertEqual(payload, original)
        self.assertEqual(result["reading"]["sequence"], 99)
        self.assertEqual(manager.station_details("HR-01")["event_count"], 0)
        self.assertEqual(manager.station_details("HR-02")["event_count"], 1)

        # Duplicate sequence is rejected
        with self.assertRaises(StationSequenceConflictError) as cm:
            manager.ingest("HR-02", self.packet("HR-02"))
        self.assertEqual(cm.exception.classification, "duplicate")

        # Out-of-order stale sequence is rejected
        stale_packet = self.packet("HR-02")
        stale_packet["sequence"] = 50
        with self.assertRaises(StationSequenceConflictError) as cm:
            manager.ingest("HR-02", stale_packet)
        self.assertEqual(cm.exception.classification, "stale")

        # Event count and latest accepted state unchanged after rejections
        self.assertEqual(manager.station_details("HR-02")["event_count"], 1)
        self.assertEqual(
            manager.station_details("HR-02")["latest"]["reading"]["sequence"], 99
        )

        # Higher sequence is accepted
        next_packet = self.packet("HR-02")
        next_packet["sequence"] = 100
        result2 = manager.ingest("HR-02", next_packet)
        self.assertEqual(result2["reading"]["sequence"], 100)
        self.assertEqual(manager.station_details("HR-02")["event_count"], 2)

    def test_station_sequence_isolation(self):
        manager = StationManager(seed=42)
        p1 = self.packet("HR-01")
        p1["sequence"] = 5
        res1 = manager.ingest("HR-01", p1)
        self.assertEqual(res1["reading"]["sequence"], 5)

        p2 = self.packet("HR-02")
        p2["sequence"] = 1
        res2 = manager.ingest("HR-02", p2)
        self.assertEqual(res2["reading"]["sequence"], 1)

        p3 = self.packet("HR-03")
        p3["sequence"] = 10
        res3 = manager.ingest("HR-03", p3)
        self.assertEqual(res3["reading"]["sequence"], 10)

    def test_identity_and_invalid_inputs_are_rejected(self):
        manager = StationManager(seed=42)
        with self.assertRaises(ValueError):
            manager.simulate("HR-01", "unknown")
        with self.assertRaises(KeyError):
            manager.simulate("HR-99", "normal")
        with self.assertRaises(ValueError):
            manager.ingest("HR-02", self.packet("HR-01"))

    def test_history_is_bounded_and_event_snapshots_are_immutable(self):
        manager = StationManager(seed=42)
        for _ in range(MAX_EVENTS + 5):
            manager.simulate("HR-01", "normal")
        self.assertEqual(manager.station_details("HR-01")["event_count"], MAX_EVENTS)
        self.assertEqual(len(manager.overview()["events"]), MAX_EVENTS)
        first = manager.station_details("HR-01")["events"][0]
        first["reading"]["node_id"] = "MUTATED"
        self.assertNotEqual(
            manager.station_details("HR-01")["events"][0]["reading"]["node_id"],
            "MUTATED",
        )


if __name__ == "__main__":
    unittest.main()
