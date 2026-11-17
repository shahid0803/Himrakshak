from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FrontendContractTests(unittest.TestCase):
    def test_landing_page_contains_edge_architecture_and_virtual_lab(self):
        source = (ROOT / "index.html").read_text(encoding="utf-8")
        for marker in (
            "Autonomous Edge Intelligence",
            "Virtual Hardware Laboratory",
            "wokwi-config.js",
            "COMMUNICATION_DEGRADED",
            "Sensor inputs",
            "Simulator not configured",
            "Multi-Parameter Geotechnical Intelligence",
            "on-device synthetic TinyML demonstration",
            "data-v2-scenario",
            "rainfall_history_mode",
            "Prototype disclosure",
            "soil-temperature proxy",
            "pore-pressure proxy",
        ):
            self.assertIn(marker, source)

    def test_wokwi_configuration_has_all_three_nodes_without_fabricated_urls(self):
        source = (ROOT / "wokwi-config.js").read_text(encoding="utf-8")
        for station in ("HR-01", "HR-02", "HR-03"):
            self.assertIn('"' + station + '"', source)
        self.assertNotIn("wokwi.com/projects/", source)

    def test_root_and_pages_entry_points_are_identical(self):
        self.assertEqual(
            (ROOT / "index.html").read_bytes(),
            (ROOT / "site" / "index.html").read_bytes(),
        )


if __name__ == "__main__":
    unittest.main()
