from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from himrakshak.data_intelligence.collectors.dem import process_dem
from himrakshak.data_intelligence.collectors.imd import climatology, import_csv
from himrakshak.data_intelligence.collectors.landslides import (
    import_csv as import_landslides_csv,
    import_geojson,
    inventory_metadata,
)
from himrakshak.data_intelligence.processors.spatial import Corridor
from himrakshak.data_intelligence.storage import IntelligenceStore


class Phase182Tests(unittest.TestCase):
    def test_csv_and_geojson_inventory_filter_and_deduplicate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            csv_path = root / "events.csv"
            csv_path.write_text(
                "event_id,longitude,latitude,event_date\n"
                "a,75.10,33.20,2024-07-01\n"
                "b,75.10,33.20,2024-07-01\n"
                "off,76.10,34.20,2024-07-01\n",
                encoding="utf-8",
            )
            corridor = Corridor(((75.0, 33.0), (75.2, 33.4)), buffer_m=1000)
            records = import_landslides_csv(csv_path, corridor=corridor)
            self.assertEqual(len(records), 2)
            self.assertEqual(inventory_metadata(records)["duplicate_records"], 1)

            geojson_path = root / "events.geojson"
            geojson_path.write_text(json.dumps({
                "type": "FeatureCollection",
                "features": [{
                    "type": "Feature",
                    "properties": {"event_id": "g", "event_date": "2024-07-02"},
                    "geometry": {"type": "Point", "coordinates": [75.1, 33.2]},
                }],
            }), encoding="utf-8")
            self.assertEqual(len(import_geojson(geojson_path, corridor=corridor)), 1)

    def test_ascii_dem_derivatives_require_projected_crs(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "dem.asc"
            path.write_text(
                "ncols 3\nnrows 3\nxllcorner 500000\nyllcorner 3650000\n"
                "cellsize 30\nNODATA_value -9999\n1 2 3\n2 3 4\n3 4 5\n",
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                process_dem(path, crs="EPSG:4326")
            features = process_dem(path, crs="EPSG:32643")
            self.assertEqual(len(features), 9)
            self.assertIsNotNone(features[4]["slope_angle"])
            self.assertIsNotNone(features[4]["terrain_curvature"])

    def test_imd_csv_preserves_missing_values_and_climatology(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rain.csv"
            path.write_text(
                "date,rainfall_mm\n2024-01-01,3\n2024-01-02,-999\n2024-02-01,5\n",
                encoding="utf-8",
            )
            records = import_csv(path, longitude=75.1, latitude=33.2)
            self.assertIsNone(records[1]["value"])
            self.assertEqual(climatology(records)[1]["observations"], 1)

    def test_landslide_records_are_persisted_with_provenance(self):
        store = IntelligenceStore(":memory:")
        records = [{
            "event_id": "a", "longitude": 75.1, "latitude": 33.2,
            "event_date": "2024-07-01T00:00:00", "source": "NRSC",
            "attributes": {}, "is_duplicate": False,
        }]
        self.assertEqual(store.insert_landslides(records), 1)
        with store.connect() as connection:
            row = connection.execute("SELECT source, event_date FROM landslide_inventory").fetchone()
        self.assertEqual(dict(row)["source"], "NRSC")


if __name__ == "__main__":
    unittest.main()
