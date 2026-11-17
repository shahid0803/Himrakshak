"""Common validation and provenance rules for external observations."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import math
from typing import Any, Mapping

from .registry import DEFAULT_STUDY_AREA, PARAMETER_REGISTRY


@dataclass(frozen=True)
class QualityResult:
    status: str
    messages: tuple[str, ...] = ()


def validate_coordinate(longitude: Any, latitude: Any) -> QualityResult:
    if isinstance(longitude, bool) or isinstance(latitude, bool):
        return QualityResult("invalid", ("coordinates must be numeric",))
    try:
        lon, lat = float(longitude), float(latitude)
    except (TypeError, ValueError):
        return QualityResult("invalid", ("coordinates must be numeric",))
    if not math.isfinite(lon) or not math.isfinite(lat) or not (-180 <= lon <= 180 and -90 <= lat <= 90):
        return QualityResult("invalid", ("coordinates are outside WGS84 bounds",))
    if not DEFAULT_STUDY_AREA.contains(lon, lat):
        return QualityResult("outside_study_area", ())
    return QualityResult("valid", ())


def validate_observation(observation: Mapping[str, Any]) -> QualityResult:
    parameter_id = observation.get("parameter_id")
    if parameter_id not in PARAMETER_REGISTRY:
        return QualityResult("invalid", ("unknown parameter_id",))
    coordinate = validate_coordinate(observation.get("longitude"), observation.get("latitude"))
    if coordinate.status == "invalid":
        return coordinate
    value = observation.get("value")
    if value is not None:
        try:
            if not math.isfinite(float(value)):
                return QualityResult("invalid", ("value must be finite",))
        except (TypeError, ValueError):
            return QualityResult("invalid", ("value must be numeric or null",))
    observed_at = observation.get("observed_at")
    if not isinstance(observed_at, str):
        return QualityResult("invalid", ("observed_at must be an ISO timestamp",))
    try:
        datetime.fromisoformat(observed_at.replace("Z", "+00:00"))
    except ValueError:
        return QualityResult("invalid", ("observed_at must be an ISO timestamp",))
    if observation.get("is_simulated") and observation.get("source") not in {"PYTHON_SIMULATOR", "WOKWI_SOFTWARE_SIMULATION"}:
        return QualityResult("invalid", ("simulated observations require a simulator source",))
    if coordinate.status != "valid":
        return coordinate
    return QualityResult("valid", ())


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
