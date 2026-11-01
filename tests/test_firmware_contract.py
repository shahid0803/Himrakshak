import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "wokwi_esp32_demo" / "sketch.ino"
DIAGRAM = ROOT / "wokwi_esp32_demo" / "diagram.json"
PROFILES = ROOT / "wokwi_esp32_demo" / "sender_profiles"


class FirmwareContractTests(unittest.TestCase):
    def test_firmware_uses_secure_transport_and_required_packet_fields(self):
        source = FIRMWARE.read_text(encoding="utf-8")
        for marker in (
            "#include <WiFi.h>",
            "#include <WiFiClientSecure.h>",
            "#include <HTTPClient.h>",
            "setCACert(HIMRAKSHAK_ROOT_CA)",
            "WOKWI_SOFTWARE_SIMULATION",
            "/readings",
            "MAX_PENDING_PACKETS = 10",
            "RETRY_MAX_MS",
            "battery_pct",
        ):
            self.assertIn(marker, source)
        self.assertNotIn("setInsecure", source)
        self.assertRegex(source, r"TELEMETRY_INTERVAL_MS\s*=\s*10000")

    def test_three_sender_profiles_are_independent_and_allow_scenarios(self):
        expected = {
            "hr01.h": ("HR-01", "normal"),
            "hr02.h": ("HR-02", "heavy_rain"),
            "hr03.h": ("HR-03", "slope_shift"),
        }
        for filename, (station, scenario) in expected.items():
            source = (PROFILES / filename).read_text(encoding="utf-8")
            self.assertIn(f'"{station}"', source)
            self.assertIn(f'"{scenario}"', source)

    def test_diagram_is_valid_json_and_has_battery_input(self):
        diagram = json.loads(DIAGRAM.read_text(encoding="utf-8"))
        parts = {part["id"] for part in diagram["parts"]}
        self.assertIn("potBattery", parts)
        connections = {tuple(connection[:2]) for connection in diagram["connections"]}
        self.assertIn(("potBattery:SIG", "esp:32"), connections)


if __name__ == "__main__":
    unittest.main()
