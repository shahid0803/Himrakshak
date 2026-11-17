"""Dependency-free corridor geometry and spatial matching helpers."""

from __future__ import annotations

from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Any, Iterable


def _distance_to_segment(point: tuple[float, float], a: tuple[float, float], b: tuple[float, float]) -> float:
    """Approximate WGS84 distance in metres using a local equirectangular projection."""
    lon, lat = point
    scale = 111_320.0
    cos_lat = math.cos(math.radians(lat))
    px, py = lon * scale * cos_lat, lat * scale
    ax, ay = a[0] * scale * cos_lat, a[1] * scale
    bx, by = b[0] * scale * cos_lat, b[1] * scale
    dx, dy = bx - ax, by - ay
    length_squared = dx * dx + dy * dy
    if not length_squared:
        return math.hypot(px - ax, py - ay)
    position = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length_squared))
    return math.hypot(px - (ax + position * dx), py - (ay + position * dy))


@dataclass(frozen=True)
class Corridor:
    coordinates: tuple[tuple[float, float], ...]
    buffer_m: float = 1_000.0
    source: str = "manual_verified_alignment"
    crs: str = "EPSG:4326"

    def contains(self, longitude: float, latitude: float) -> bool:
        return any(
            _distance_to_segment((longitude, latitude), start, end) <= self.buffer_m
            for start, end in zip(self.coordinates, self.coordinates[1:])
        )

    def metadata(self) -> dict[str, Any]:
        return {
            "source": self.source,
            "crs": self.crs,
            "buffer_m": self.buffer_m,
            "vertex_count": len(self.coordinates),
            "verified": self.source != "approximate_bbox",
        }


def load_corridor_geojson(path: str | Path, *, buffer_m: float = 1_000.0) -> Corridor:
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    geometry = payload.get("geometry", payload)
    if payload.get("type") == "Feature":
        geometry = payload["geometry"]
    if geometry.get("type") != "LineString":
        raise ValueError("corridor GeoJSON must contain a LineString geometry")
    coordinates = tuple((float(point[0]), float(point[1])) for point in geometry["coordinates"])
    if len(coordinates) < 2:
        raise ValueError("corridor requires at least two vertices")
    return Corridor(coordinates, buffer_m, source=str(path))


def filter_points(
    records: Iterable[dict[str, Any]],
    corridor: Corridor,
    *,
    longitude_key: str = "longitude",
    latitude_key: str = "latitude",
) -> list[dict[str, Any]]:
    result = []
    for record in records:
        if corridor.contains(float(record[longitude_key]), float(record[latitude_key])):
            result.append(record)
    return result
