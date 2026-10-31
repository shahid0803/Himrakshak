import json
from http.client import HTTPConnection
import threading
import unittest

from himrakshak.server import HimRakshakServer


class DemoAcceptanceTests(unittest.TestCase):
    """One repeatable, high-level HTTP smoke test for the full demonstration."""

    def setUp(self):
        self.server = HimRakshakServer(("127.0.0.1", 0), seed=42)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.host, self.port = self.server.server_address

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def request(self, method, path, body=None):
        connection = HTTPConnection(self.host, self.port, timeout=3)
        encoded = None if body is None else json.dumps(body).encode("utf-8")
        headers = {"Connection": "close"}
        if body is not None:
            headers["Content-Type"] = "application/json"
        connection.request(method, path, body=encoded, headers=headers)
        response = connection.getresponse()
        raw = response.read()
        connection.close()
        return response.status, json.loads(raw) if raw else None

    @staticmethod
    def packet(node_id, sequence, moisture=32.4):
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

    def test_repeatable_demo_acceptance_flow(self):
        status, health = self.request("GET", "/api/health")
        self.assertEqual(status, 200)
        self.assertEqual(health["station_count"], 3)

        status, overview = self.request("GET", "/api/stations")
        self.assertEqual(status, 200)
        self.assertEqual(
            [station["node_id"] for station in overview["stations"]],
            ["HR-01", "HR-02", "HR-03"],
        )

        status, normal = self.request(
            "POST", "/api/stations/HR-01/simulate", {"scenario": "normal"}
        )
        self.assertEqual(status, 200)
        self.assertEqual(normal["assessment"]["risk_level"], "NORMAL")
        before_drop = self.request("GET", "/api/stations/HR-01")[1]["station"]

        status, dropped = self.request(
            "POST",
            "/api/stations/HR-01/faults/drop",
            self.packet("HR-01", 1, moisture=80),
        )
        self.assertEqual(status, 200)
        self.assertEqual(dropped["outcome"], "dropped")
        after_drop = self.request("GET", "/api/stations/HR-01")[1]["station"]
        self.assertEqual(after_drop["latest"], before_drop["latest"])
        self.assertEqual(after_drop["next_sequence"], before_drop["next_sequence"])
        self.assertEqual(after_drop["event_count"], before_drop["event_count"])

        for sequence in (1, 2):
            status, pending = self.request(
                "POST",
                "/api/stations/HR-01/faults/delay",
                self.packet("HR-01", sequence),
            )
            self.assertEqual(status, 200)
            self.assertEqual(pending["outcome"], "pending")
        detail = self.request("GET", "/api/stations/HR-01")[1]["station"]
        self.assertEqual(detail["communication"]["pending_packets"], 2)

        status, first_release = self.request(
            "POST", "/api/stations/HR-01/faults/release", {}
        )
        self.assertEqual(status, 200)
        self.assertEqual(first_release["released_order"], 1)
        self.assertEqual(first_release["reading"]["sequence"], 1)
        status, second_release = self.request(
            "POST", "/api/stations/HR-01/faults/release", {}
        )
        self.assertEqual(status, 200)
        self.assertEqual(second_release["released_order"], 2)
        self.assertEqual(second_release["reading"]["sequence"], 2)

        status, duplicate = self.request(
            "POST",
            "/api/stations/HR-01/faults/duplicate",
            self.packet("HR-01", 2),
        )
        self.assertEqual(status, 200)
        self.assertEqual(duplicate["outcome"], "duplicate")
        status, stale = self.request(
            "POST",
            "/api/stations/HR-01/faults/stale",
            self.packet("HR-01", 1),
        )
        self.assertEqual(status, 200)
        self.assertEqual(stale["outcome"], "stale")

        for sequence in range(10):
            status, _ = self.request(
                "POST",
                "/api/stations/HR-02/faults/delay",
                self.packet("HR-02", sequence),
            )
            self.assertEqual(status, 200)
        status, queue_error = self.request(
            "POST",
            "/api/stations/HR-02/faults/delay",
            self.packet("HR-02", 10),
        )
        self.assertEqual(status, 409)
        self.assertIn("error", queue_error)

        status, _ = self.request(
            "POST", "/api/stations/HR-03/faults/disconnect", {}
        )
        self.assertEqual(status, 200)
        status, disconnected = self.request(
            "POST", "/api/stations/HR-03/simulate", {"scenario": "normal"}
        )
        self.assertEqual(status, 409)
        self.assertIn("error", disconnected)
        status, _ = self.request(
            "POST", "/api/stations/HR-03/faults/reconnect", {}
        )
        self.assertEqual(status, 200)
        status, recovered = self.request(
            "POST", "/api/stations/HR-03/simulate", {"scenario": "normal"}
        )
        self.assertEqual(status, 200)
        self.assertEqual(recovered["reading"]["sequence"], 0)

        before_invalid = self.request("GET", "/api/stations/HR-01")[1]["station"]
        status, invalid = self.request(
            "POST",
            "/api/stations/HR-01/faults/delay",
            {"reading": {"node_id": "HR-01"}},
        )
        self.assertEqual(status, 400)
        self.assertIn("error", invalid)
        after_invalid = self.request("GET", "/api/stations/HR-01")[1]["station"]
        self.assertEqual(after_invalid, before_invalid)

        status, heavy_rain = self.request(
            "POST", "/api/simulate", {"scenario": "heavy_rain"}
        )
        self.assertEqual(status, 200)
        self.assertEqual(heavy_rain["reading"]["node_id"], "HR-01")
        self.assertEqual(heavy_rain["assessment"]["risk_level"], "CRITICAL")


if __name__ == "__main__":
    unittest.main()
