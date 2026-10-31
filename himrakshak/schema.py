"""Validation for canonical HimRakshak sensor readings.

This module validates and lightly normalizes incoming sensor data. It does
not calculate risk scores or decide whether a slope is safe.
"""

from __future__ import annotations

from datetime import datetime, timezone
import math
from numbers import Real
from typing import Any, Mapping


class SchemaValidationError(ValueError):
    """Raised when a sensor reading does not satisfy the data contract."""


REQUIRED_FIELDS = (
    "node_id",
    "sequence",
    "timestamp",
    "soil_moisture_pct",
    "tilt_deg",
    "vibration_level",
    "battery_pct",
)

NUMERIC_RANGES = {
    "soil_moisture_pct": (0.0, 100.0),
    "tilt_deg": (0.0, 15.0),
    "vibration_level": (0.0, 100.0),
    "battery_pct": (0.0, 100.0),
}

LEGACY_ALIASES = {
    "node_id": "node",
    "soil_moisture_pct": "moisture",
    "tilt_deg": "tilt",
    "vibration_level": "vibration",
}

WOKWI_TRANSPORT = "WOKWI_SOFTWARE_SIMULATION"
WOKWI_TIMESTAMP = "SIMULATED"


def _error(field: str, message: str) -> SchemaValidationError:
    return SchemaValidationError(f"{field}: {message}")


def _validate_number(field: str, value: Any, minimum: float, maximum: float) -> None:
    if isinstance(value, bool) or not isinstance(value, Real):
        raise _error(field, "must be a finite number")
    if not math.isfinite(float(value)):
        raise _error(field, "must be a finite number")
    if not minimum <= float(value) <= maximum:
        raise _error(field, f"must be between {minimum:g} and {maximum:g}")


def _validate_timestamp(payload: Mapping[str, Any]) -> str:
    timestamp = payload["timestamp"]
    if not isinstance(timestamp, str) or not timestamp:
        raise _error("timestamp", "must be an ISO 8601 string")

    transport = payload.get("transport")
    if timestamp == WOKWI_TIMESTAMP:
        if transport != WOKWI_TRANSPORT:
            raise _error(
                "timestamp",
                "SIMULATED is allowed only with "
                f"transport={WOKWI_TRANSPORT!r}",
            )
        return timestamp

    try:
        parsed = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
    except ValueError as exc:
        raise _error("timestamp", "must be a valid ISO 8601 timestamp") from exc
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise _error("timestamp", "must include a timezone offset")
    return parsed.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def _normalize_aliases(payload: Mapping[str, Any]) -> dict[str, Any]:
    normalized = dict(payload)
    for canonical, alias in LEGACY_ALIASES.items():
        has_canonical = canonical in payload
        has_alias = alias in payload
        if has_canonical and has_alias:
            canonical_value = payload[canonical]
            alias_value = payload[alias]
            if canonical == "node_id":
                if (
                    not isinstance(canonical_value, str)
                    or not isinstance(alias_value, str)
                    or canonical_value.strip() != alias_value.strip()
                ):
                    raise _error(canonical, f"conflicts with legacy alias {alias}")
            else:
                minimum, maximum = NUMERIC_RANGES[canonical]
                _validate_number(canonical, canonical_value, minimum, maximum)
                _validate_number(alias, alias_value, minimum, maximum)
                if canonical_value != alias_value:
                    raise _error(canonical, f"conflicts with legacy alias {alias}")
        elif has_alias:
            normalized[canonical] = payload[alias]
        normalized.pop(alias, None)
    return normalized


def validate_reading(payload: Mapping[str, Any]) -> dict[str, Any]:
    """Validate and normalize one canonical sensor reading.

    The returned dictionary is a shallow copy. ``node_id`` is trimmed, while
    all other supplied fields retain their values. Unknown metadata, including
    derived risk fields, is retained but never trusted or recalculated.
    """

    if not isinstance(payload, Mapping):
        raise SchemaValidationError("payload: must be a dictionary-like object")

    normalized = _normalize_aliases(payload)
    missing = [field for field in REQUIRED_FIELDS if field not in normalized]
    if missing:
        raise SchemaValidationError(
            "payload: missing required field(s): " + ", ".join(missing)
        )

    node_id = normalized["node_id"]
    if not isinstance(node_id, str):
        raise _error("node_id", "must be a string")
    node_id = node_id.strip()
    if not node_id:
        raise _error("node_id", "must not be empty")
    if len(node_id) > 40:
        raise _error("node_id", "must be at most 40 characters")

    sequence = normalized["sequence"]
    if isinstance(sequence, bool) or not isinstance(sequence, int):
        raise _error("sequence", "must be a non-negative integer")
    if sequence < 0:
        raise _error("sequence", "must be a non-negative integer")

    normalized_timestamp = _validate_timestamp(normalized)

    for field, (minimum, maximum) in NUMERIC_RANGES.items():
        _validate_number(field, normalized[field], minimum, maximum)

    transport = normalized.get("transport")
    if transport is not None and (
        not isinstance(transport, str) or not transport.strip()
    ):
        raise _error("transport", "must be a non-empty string when supplied")

    normalized["node_id"] = node_id
    normalized["timestamp"] = normalized_timestamp
    return normalized
