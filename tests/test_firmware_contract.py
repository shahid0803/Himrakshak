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
            "configTime",
            "synchronizeClock",
            "synchronizeClock();\n  synchronizeStartupSequence();",
            "TLS_HOST",
            "diagnoseTransportFailure",
            "DNS failure",
            "TCP failure",
            "TLS failure verifying",
            "HTTP POST status",
            "WOKWI_SOFTWARE_SIMULATION",
            "/readings",
            "MAX_PENDING_PACKETS = 10",
            "RETRY_MAX_MS",
            "battery_pct",
        ):
            self.assertIn(marker, source)
        self.assertNotIn("setInsecure", source)
        self.assertRegex(source, r"TELEMETRY_INTERVAL_MS\s*=\s*10000")

    def test_firmware_embeds_the_verified_render_root(self):
        root = (ROOT / "wokwi_esp32_demo" / "gts_root_r4.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("R1RTIFJvb3QgUjQ", root)
        self.assertNotIn("ISRG Root X1", root)

    def test_firmware_reconciles_structured_409_conflicts(self):
        source = FIRMWARE.read_text(encoding="utf-8")
        for marker in (
            'GET station state',
            'String(INGEST_URL) + HIMRAKSHAK_STATION_ID',
            'classification',
            'reconcileConflict',
            'readingsMatch',
            'alreadyDeliveredPackets',
            'supersededPackets',
            'blockedPackets',
            'Startup sequence synchronized',
            'pending sequence is newer than backend sequence',
            'Sequence reuse conflict',
            'receiver disconnected during 409 reconciliation',
        ):
            self.assertIn(marker, source)
        self.assertRegex(source, r'if \(packet\.sequence == state\.latestSequence')
        self.assertRegex(source, r'if \(packet\.sequence < state\.latestSequence')

    def test_backend_sequence_protection_contract_remains_documented(self):
        api_contract = (ROOT / "docs" / "api-contract.md").read_text(encoding="utf-8")
        self.assertIn("preserves the incoming device-assigned `sequence`", api_contract)
        self.assertIn("duplicate", api_contract)
        self.assertIn("stale", api_contract)
        self.assertIn("HTTP 409", api_contract)

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
