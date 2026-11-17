"""Historical landslide inventory import with provenance and deduplication."""

from __future__ import annotations

import csv
from datetime import datetime
import json
from pathlib import Path
from typing import Any

from ..registry import StudyArea, DEFAULT_STUDY_AREA
from ..processors.spatial import Corridor, filter_points


_LONGITUDE_KEYS = ("longitude", "lon", "lng", "x")
_LATITUDE_KEYS = ("latitude", "lat", "y")
_DATE_KEYS = ("event_date", "date", "date_of_occurrence", "event_time")
_ID_KEYS = ("event_id", "id", "landslide_id", "landslide")


def _first(row: dict[str, Any], keys: tuple[str, ...]) -> Any:
    lowered = {str(key).strip().lower(): value for key, value in row.items()}
    for key in keys:
        if lowered.get(key) not in (None, ""):
            return lowered[key]
    return None


def _normalise(row: dict[str, Any], *, source: str) -> dict[str, Any]:
    longitude = _first(row, _LONGITUDE_KEYS)
    latitude = _first(row, _LATITUDE_KEYS)
    try:
        longitude, latitude = float(longitude), float(latitude)
    except (TypeError, ValueError) as exc:
        raise ValueError("inventory must contain numeric longitude and latitude columns") from exc
    event_date = _first(row, _DATE_KEYS)
    if event_date:
        try:
            event_date = datetime.fromisoformat(str(event_date).replace("Z", "+00:00")).isoformat()
        except ValueError:
            raise ValueError(f"invalid event date: {event_date!r}") from None
    return {
        "event_id": str(_first(row, _ID_KEYS) or f"{longitude:.6f},{latitude:.6f},{event_date or ''}"),
        "longitude": longitude,
        "latitude": latitude,
        "event_date": event_date,
        "source": source,
        "attributes": dict(row),
    }


def _deduplicate(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    seen: set[tuple[Any, ...]] = set()
    result = []
    for record in records:
        key = (
            round(record["longitude"], 5),
            round(record["latitude"], 5),
            record.get("event_date"),
        )
        record = dict(record)
        record["is_duplicate"] = key in seen
        seen.add(key)
        result.append(record)
    return result


def import_csv(
    path: str | Path,
    *,
    area: StudyArea = DEFAULT_STUDY_AREA,
    corridor: Corridor | None = None,
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    with Path(path).open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            record = _normalise(row, source=str(row.get("source") or "NRSC"))
            if area.contains(record["longitude"], record["latitude"]):
                result.append(record)
    if corridor is not None:
        result = filter_points(result, corridor)
    return _deduplicate(result)


def import_geojson(
    path: str | Path,
    *,
    area: StudyArea = DEFAULT_STUDY_AREA,
    corridor: Corridor | None = None,
) -> list[dict[str, Any]]:
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    features = payload.get("features", []) if payload.get("type") == "FeatureCollection" else [payload]
    records: list[dict[str, Any]] = []
    for feature in features:
        geometry = feature.get("geometry") or {}
        coordinates = geometry.get("coordinates")
        if geometry.get("type") == "Point":
            longitude, latitude = coordinates[:2]
        elif geometry.get("type") in {"Polygon", "MultiPoint"}:
            points = coordinates[0] if geometry["type"] == "Polygon" else coordinates
            longitude = sum(point[0] for point in points) / len(points)
            latitude = sum(point[1] for point in points) / len(points)
        else:
            raise ValueError("inventory GeoJSON supports Point, Polygon, and MultiPoint geometries")
        properties = dict(feature.get("properties") or {})
        properties.update({"longitude": longitude, "latitude": latitude})
        record = _normalise(properties, source=str(properties.get("source") or "NRSC"))
        if area.contains(longitude, latitude):
            records.append(record)
    if corridor is not None:
        records = filter_points(records, corridor)
    return _deduplicate(records)


def import_shapefile(path: str | Path, **kwargs: Any) -> list[dict[str, Any]]:
    try:
        import shapefile  # type: ignore
    except ImportError as exc:
        raise RuntimeError("Shapefile import requires optional pyshp; use GeoJSON/CSV without it") from exc
    reader = shapefile.Reader(str(path))
    fields = [field[0] for field in reader.fields[1:]]
    records = []
    for shape_record in reader.iterShapeRecords():
        if shape_record.shape.shapeType not in {1, 11, 21}:
            raise ValueError("shapefile inventory import currently requires Point geometries")
        row = dict(zip(fields, shape_record.record))
        row.update({"longitude": shape_record.shape.points[0][0], "latitude": shape_record.shape.points[0][1]})
        records.append(_normalise(row, source=str(row.get("source") or "NRSC")))
    area = kwargs.get("area", DEFAULT_STUDY_AREA)
    corridor = kwargs.get("corridor")
    records = [record for record in records if area.contains(record["longitude"], record["latitude"])]
    return _deduplicate(filter_points(records, corridor) if corridor else records)


def inventory_metadata(records: list[dict[str, Any]], *, source: str = "NRSC") -> dict[str, Any]:
    return {
        "source": source,
        "records": len(records),
        "unique_events": sum(not record.get("is_duplicate") for record in records),
        "duplicate_records": sum(bool(record.get("is_duplicate")) for record in records),
        "with_event_date": sum(bool(record.get("event_date")) for record in records),
        "completeness": (sum(bool(record.get("event_date")) for record in records) / len(records)) if records else 0.0,
    }
