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
        ):
            self.assertIn(marker, source)

    def test_wokwi_configuration_has_all_three_nodes_without_fabricated_urls(self):
        source = (ROOT / "wokwi-config.js").read_text(encoding="utf-8")
        for station in ("HR-01", "HR-02", "HR-03"):
            self.assertIn('"' + station + '"', source)
        self.assertNotIn("wokwi.com/projects/", source)


if __name__ == "__main__":
    unittest.main()
