"""IMD rainfall imports and climatology."""

from __future__ import annotations

import csv
from collections import defaultdict
from datetime import datetime
from pathlib import Path
from typing import Any


def import_csv(path: str | Path, *, longitude: float, latitude: float) -> list[dict[str, Any]]:
    """Import a downloaded IMD CSV without filling missing dates."""
    records = []
    with Path(path).open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            timestamp = row.get("observed_at") or row.get("date") or row.get("timestamp")
            value = row.get("rainfall_mm") or row.get("rainfall") or row.get("value")
            if not timestamp:
                raise ValueError("IMD CSV requires observed_at, date, or timestamp")
            try:
                parsed = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
                numeric = None if value in (None, "", "-999", "-9999") else float(value)
            except ValueError as exc:
                raise ValueError(f"invalid IMD row: {row}") from exc
            records.append({
                "observed_at": parsed.isoformat(),
                "longitude": longitude,
                "latitude": latitude,
                "value": numeric,
                "unit": "mm",
                "source": "IMD",
                "source_product": "gridded_rainfall",
                "spatial_resolution": row.get("spatial_resolution"),
                "is_simulated": False,
            })
    return records


def climatology(records: list[dict[str, Any]]) -> dict[int, dict[str, float | int | None]]:
    grouped: dict[int, list[float]] = defaultdict(list)
    for record in records:
        if record.get("value") is None:
            continue
        month = datetime.fromisoformat(str(record["observed_at"]).replace("Z", "+00:00")).month
        grouped[month].append(float(record["value"]))
    return {
        month: {
            "observations": len(values),
            "mean_mm": sum(values) / len(values) if values else None,
            "total_mm": sum(values),
        }
        for month, values in sorted(grouped.items())
    }


def import_imd_archive(path: str | Path, **kwargs: Any):
    if not Path(path).is_file():
        raise FileNotFoundError(f"IMD archive does not exist: {path}")
    if Path(path).suffix.lower() == ".csv":
        if "longitude" not in kwargs or "latitude" not in kwargs:
            raise ValueError("longitude and latitude are required for an IMD CSV")
        return import_csv(path, longitude=float(kwargs["longitude"]), latitude=float(kwargs["latitude"]))
    try:
        import xarray as xr  # type: ignore
    except ImportError as exc:
        raise RuntimeError("NetCDF parsing requires optional xarray/netCDF4; CSV import remains available") from exc
    dataset = xr.open_dataset(path)
    return {"dataset": dataset, "source": "IMD", "format": "NetCDF"}
