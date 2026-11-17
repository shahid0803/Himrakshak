"""GSI geology layer importer for GeoJSON and simple CSV exports."""
from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any


def _record(properties: dict[str, Any], longitude: float, latitude: float, source: str) -> dict[str, Any]:
    formation = properties.get("geological_formation") or properties.get("formation") or properties.get("FORMATION")
    lithology = properties.get("lithology") or properties.get("LITHOLOGY") or properties.get("rock_type")
    if not formation and not lithology:
        raise ValueError("geology feature must include formation or lithology")
    if not (-180 <= longitude <= 180 and -90 <= latitude <= 90):
        raise ValueError("geology feature has invalid geographic coordinates")
    return {
        "longitude": longitude,
        "latitude": latitude,
        "geological_formation": str(formation) if formation is not None else None,
        "lithology": str(lithology) if lithology is not None else None,
        "source": source,
        "source_product": "GSI_Bhukosh_manual_export",
        "attributes": properties,
    }


def import_gsi_layer(path: str | Path) -> list[dict[str, Any]]:
    source_path = Path(path)
    if not source_path.is_file():
        raise FileNotFoundError(f"GSI layer does not exist: {source_path}")
    suffix = source_path.suffix.lower()
    if suffix in {".geojson", ".json"}:
        payload = json.loads(source_path.read_text(encoding="utf-8"))
        features = payload.get("features", []) if payload.get("type") == "FeatureCollection" else [payload]
        output = []
        for feature in features:
            geometry = feature.get("geometry") or {}
            if geometry.get("type") != "Point":
                raise ValueError("geology GeoJSON import currently requires Point geometries")
            coordinates = geometry.get("coordinates") or []
            if len(coordinates) < 2:
                raise ValueError("geology Point is missing coordinates")
            output.append(_record(dict(feature.get("properties") or {}), float(coordinates[0]), float(coordinates[1]), "GSI"))
        return output
    if suffix == ".csv":
        output = []
        with source_path.open(newline="", encoding="utf-8-sig") as handle:
            for line, row in enumerate(csv.DictReader(handle), start=2):
                try:
                    longitude = float(row.get("longitude", row.get("lon", "")))
                    latitude = float(row.get("latitude", row.get("lat", "")))
                except (TypeError, ValueError) as exc:
                    raise ValueError(f"invalid geology coordinates on line {line}") from exc
                output.append(_record(dict(row), longitude, latitude, "GSI"))
        return output
    raise ValueError("GSI layer must be GeoJSON or CSV; convert shapefiles to GeoJSON first")
