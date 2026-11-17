"""Groundwater CSV importer with explicit depth and coordinate fields."""
from __future__ import annotations

import csv
from pathlib import Path
from typing import Any


def import_csv(
    path: str | Path,
    *,
    longitude_field: str = "longitude",
    latitude_field: str = "latitude",
    depth_field: str = "groundwater_depth_m",
    observed_at_field: str = "observed_at",
    station_field: str = "station_id",
) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with Path(path).open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        required = {longitude_field, latitude_field, depth_field, observed_at_field}
        missing = required - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"groundwater CSV is missing columns: {sorted(missing)}")
        for line_number, row in enumerate(reader, start=2):
            try:
                longitude = float(row[longitude_field])
                latitude = float(row[latitude_field])
                depth = float(row[depth_field])
            except (TypeError, ValueError) as exc:
                raise ValueError(f"invalid groundwater numeric value on line {line_number}") from exc
            if not (-180 <= longitude <= 180 and -90 <= latitude <= 90):
                raise ValueError(f"invalid coordinates on line {line_number}")
            if not (0 <= depth <= 1000):
                raise ValueError(f"groundwater depth out of range on line {line_number}")
            timestamp = str(row[observed_at_field]).strip()
            if not timestamp:
                raise ValueError(f"missing observed_at on line {line_number}")
            rows.append({
                "longitude": longitude,
                "latitude": latitude,
                "groundwater_depth": depth,
                "unit": "m",
                "observed_at": timestamp,
                "station_id": (row.get(station_field) or None),
                "parameter_id": "groundwater_depth",
                "source": "manual_groundwater_csv",
                "source_product": Path(path).name,
                "is_simulated": False,
            })
    return rows
