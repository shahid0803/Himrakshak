import copy
import unittest

from himrakshak.reliability import MAX_PENDING_PACKETS
from himrakshak.stations import (
    PendingQueueEmptyError,
    PendingQueueFullError,
    StationDisconnectedError,
    StationManager,
)


class ReliabilityTests(unittest.TestCase):
    def packet(self, node_id, sequence, moisture=32.4):
        return {
            "node_id": node_id,
            "sequence": sequence,
            "timestamp": "2026-10-28T03:30:00Z",
            "soil_moisture_pct": moisture,
            "tilt_deg": 0.9,
            "vibration_level": 11.1,
            "battery_pct": 82.0,
            "transport": "PYTHON_SIMULATOR",
        }

    def test_drop_does_not_change_accepted_state(self):
        manager = StationManager(seed=42)
        accepted = manager.deliver_packet("HR-01", self.packet("HR-01", 0))
        before = manager.station_details("HR-01")
        outcome = manager.drop_packet("HR-01", self.packet("HR-01", 1, 80))
        after = manager.station_details("HR-01")
        self.assertEqual(accepted["outcome"], "delivered")
        self.assertEqual(outcome["outcome"], "dropped")
        self.assertEqual(after["latest"], before["latest"])
        self.assertEqual(after["event_count"], before["event_count"])
        self.assertEqual(after["communication"]["packets_dropped"], 1)

    def test_delayed_packets_release_fifo_and_are_bounded(self):
        manager = StationManager(seed=42)
        first = manager.delay_packet("HR-01", self.packet("HR-01", 0))
        second = manager.delay_packet("HR-01", self.packet("HR-01", 1, 70))
        self.assertEqual(first["outcome"], "pending")
        self.assertEqual(second["outcome"], "pending")
        self.assertIsNone(manager.station_details("HR-01")["latest"])
        released = manager.release_packet("HR-01")
        self.assertEqual(released["outcome"], "delivered")
        self.assertEqual(released["released_order"], 1)
        self.assertEqual(released["reading"]["sequence"], 0)
        released = manager.release_packet("HR-01")
        self.assertEqual(released["released_order"], 2)
        self.assertEqual(released["reading"]["sequence"], 1)
        with self.assertRaises(PendingQueueEmptyError):
            manager.release_packet("HR-01")

        for sequence in range(MAX_PENDING_PACKETS):
            manager.delay_packet("HR-02", self.packet("HR-02", sequence))
        with self.assertRaises(PendingQueueFullError):
            manager.delay_packet(
                "HR-02", self.packet("HR-02", MAX_PENDING_PACKETS)
            )

    def test_duplicate_and_stale_are_rejected_without_state_change(self):
        manager = StationManager(seed=42)
        manager.deliver_packet("HR-01", self.packet("HR-01", 0))
        manager.deliver_packet("HR-01", self.packet("HR-01", 1, 75))
        before = copy.deepcopy(manager.station_details("HR-01"))
        duplicate = manager.deliver_packet("HR-01", self.packet("HR-01", 1))
        stale = manager.deliver_packet("HR-01", self.packet("HR-01", 0))
        after = manager.station_details("HR-01")
        self.assertEqual(duplicate["outcome"], "duplicate")
        self.assertEqual(stale["outcome"], "stale")
        self.assertEqual(after["latest"], before["latest"])
        self.assertEqual(after["event_count"], before["event_count"])
        self.assertEqual(after["communication"]["duplicate_rejections"], 1)
        self.assertEqual(after["communication"]["stale_rejections"], 1)

    def test_disconnect_blocks_delivery_and_reconnect_recovers(self):
        manager = StationManager(seed=42)
        manager.deliver_packet("HR-01", self.packet("HR-01", 0))
        manager.disconnect("HR-01")
        before = manager.station_details("HR-01")
        with self.assertRaises(StationDisconnectedError):
            manager.deliver_packet("HR-01", self.packet("HR-01", 1))
        after = manager.station_details("HR-01")
        self.assertEqual(after["latest"], before["latest"])
        self.assertEqual(after["event_count"], before["event_count"])
        self.assertEqual(after["communication"]["disconnected_attempts"], 1)
        manager.reconnect("HR-01")
        recovered = manager.deliver_packet("HR-01", self.packet("HR-01", 1))
        self.assertEqual(recovered["outcome"], "delivered")

    def test_fault_state_isolated_between_stations(self):
        manager = StationManager(seed=42)
        manager.disconnect("HR-01")
        manager.delay_packet("HR-02", self.packet("HR-02", 0))
        hr01 = manager.station_details("HR-01")
        hr02 = manager.station_details("HR-02")
        hr03 = manager.station_details("HR-03")
        self.assertEqual(hr01["communication"]["connection_state"], "disconnected")
        self.assertEqual(hr01["communication"]["pending_packets"], 0)
        self.assertEqual(hr02["communication"]["pending_packets"], 1)
        self.assertEqual(hr03["communication"]["packets_attempted"], 0)


if __name__ == "__main__":
    unittest.main()
