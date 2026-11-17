import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "wokwi_esp32_demo" / "sketch.ino"
DIAGRAM = ROOT / "wokwi_esp32_demo" / "diagram.json"
PROFILES = ROOT / "wokwi_esp32_demo" / "sender_profiles"


class FirmwareContractTests(unittest.TestCase):
    def setUp(self):
        self.source = FIRMWARE.read_text(encoding="utf-8")

    def test_secure_transport_and_packet_contract_are_preserved(self):
        for marker in (
            "#include <WiFi.h>",
            "#include <WiFiClientSecure.h>",
            "#include <HTTPClient.h>",
            "setCACert(HIMRAKSHAK_ROOT_CA)",
            "configTime",
            "synchronizeClock",
            "synchronizeStartupSequence",
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
            self.assertIn(marker, self.source)
        self.assertNotIn("setInsecure", self.source)
        self.assertRegex(self.source, r"TELEMETRY_INTERVAL_MS\s*=\s*10000")

    def test_firmware_embeds_the_verified_render_root(self):
        root = (ROOT / "wokwi_esp32_demo" / "gts_root_r4.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("R1RTIFJvb3QgUjQ", root)
        self.assertNotIn("ISRG Root X1", root)

    def test_structured_409_reconciliation_is_preserved(self):
        for marker in (
            "GET station state",
            'String(INGEST_URL) + HIMRAKSHAK_STATION_ID',
            'String(INGEST_URL) + HIMRAKSHAK_STATION_ID + "/readings"',
            'classification',
            'reconcileConflict',
            'readingsMatch',
            'alreadyDeliveredPackets',
            'supersededPackets',
            'blockedPackets',
            'Sequence reuse conflict',
            'receiver disconnected during 409 reconciliation',
        ):
            self.assertIn(marker, self.source)
        self.assertIn("rebaseQueuedSequences", self.source)

    def test_environmental_and_communication_states_are_separate(self):
        controller_enum = re.search(
            r"enum ControllerState\s*\{([^}]+)\}", self.source, re.S
        )
        communication_enum = re.search(
            r"enum CommunicationState\s*\{([^}]+)\}", self.source, re.S
        )
        self.assertIsNotNone(controller_enum)
        self.assertIsNotNone(communication_enum)
        self.assertNotIn("COMMUNICATION_DEGRADED", controller_enum.group(1))
        self.assertIn("COMMUNICATION_DEGRADED", communication_enum.group(1))
        self.assertNotIn("nextState = COMMUNICATION_DEGRADED", self.source)

    def test_critical_hysteresis_is_checked_before_warning_downgrade(self):
        critical_hold = self.source.index("if (controllerState == CRITICAL")
        warning_assessment = self.source.index(
            'if (strcmp(assessment.level, "WARNING") == 0)'
        )
        self.assertLess(critical_hold, warning_assessment)
        self.assertIn("assessment.score >= 45", self.source)

    def test_sensor_fault_logic_does_not_flag_constant_readings(self):
        self.assertNotIn("SENSOR_STUCK_TIMEOUT_MS", self.source)
        self.assertNotIn("sensorStuckSince", self.source)
        self.assertIn("HIMRAKSHAK_SIMULATE_SENSOR_FAULT", self.source)
        self.assertIn("#if HIMRAKSHAK_SIMULATE_SENSOR_FAULT", self.source)

    def test_fault_indicator_does_not_show_green_as_healthy(self):
        indicator_function = self.source.split("void updateIndicators()", 1)[1]
        indicator_function = indicator_function.split("void reportCommunicationState()", 1)[0]
        self.assertIn("case SENSOR_FAULT:", indicator_function)
        self.assertIn("yellow = ((now / 400) % 2) == 0", indicator_function)
        self.assertIn("case CRITICAL:", indicator_function)
        self.assertIn("yellow = !sensorReadHealthy", indicator_function)

    def test_telemetry_uses_the_same_filtered_sensor_frame(self):
        self.assertIn(
            "String buildPacket(unsigned long sequence,\n                   const SensorFrame &frame,\n                   const Assessment &assessment)",
            self.source,
        )
        self.assertIn("buildPacket(sequenceNumber, frame, assessment)", self.source)
        packet_function = self.source.split("String buildPacket(", 1)[1]
        packet_function = packet_function.split("void reportStatus", 1)[0]
        self.assertNotIn("readSensors(", packet_function)
        self.assertIn("frame.moisture", packet_function)
        self.assertIn("frame.tiltDeg", packet_function)

    def test_startup_wifi_and_ntp_waits_are_non_blocking(self):
        wifi_function = self.source.split("bool connectWiFi()", 1)[1]
        wifi_function = wifi_function.split("bool synchronizeClock()", 1)[0]
        clock_function = self.source.split("bool synchronizeClock()", 1)[1]
        clock_function = clock_function.split("String jsonValue", 1)[0]
        self.assertNotIn("while (", wifi_function)
        self.assertNotIn("delay(", wifi_function)
        self.assertNotIn("while (", clock_function)
        self.assertNotIn("delay(", clock_function)
        self.assertIn("Start Wi-Fi without waiting", self.source)

    def test_packet_queue_can_rebase_sequences_after_offline_buffering(self):
        self.assertIn("bool replacePacketSequence", self.source)
        self.assertIn('const String marker = "\\\"sequence\\\":"', self.source)
        self.assertIn("rebaseQueuedSequences(firstSequence)", self.source)

    def test_three_sender_profiles_are_independent(self):
        expected = {
            "hr01.h": ("HR-01", "normal"),
            "hr02.h": ("HR-02", "heavy_rain"),
            "hr03.h": ("HR-03", "slope_shift"),
        }
        for filename, (station, scenario) in expected.items():
            profile = (PROFILES / filename).read_text(encoding="utf-8")
            self.assertIn(f'"{station}"', profile)
            self.assertIn(f'"{scenario}"', profile)

    def test_wokwi_diagram_is_valid_and_has_battery_input(self):
        diagram = json.loads(DIAGRAM.read_text(encoding="utf-8"))
        parts = {part["id"] for part in diagram["parts"]}
        self.assertIn("potBattery", parts)
        connections = {
            tuple(connection[:2]) for connection in diagram["connections"]
        }
        self.assertIn(("potBattery:SIG", "esp:32"), connections)


if __name__ == "__main__":
    unittest.main()
