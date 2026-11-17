"""Strict validation for the expanded V2 environmental telemetry contract."""
from __future__ import annotations

import math
from numbers import Real
from typing import Any, Mapping

from .schema import SchemaValidationError

V2_FEATURE_RANGES = {
    "rainfall_intensity_mm_h": (0.0, 200.0),
    "rainfall_1h_mm": (0.0, 300.0),
    "rainfall_24h_mm": (0.0, 2000.0),
    "soil_moisture_surface_pct": (0.0, 100.0),
    "soil_moisture_subsurface_pct": (0.0, 100.0),
    "pore_water_pressure_kpa": (0.0, 1000.0),
    "tilt_x_deg": (-90.0, 90.0),
    "tilt_y_deg": (-90.0, 90.0),
    "tilt_rate_deg_per_h": (-100.0, 100.0),
    "ground_displacement_mm": (0.0, 10000.0),
    "ground_displacement_rate_mm_per_h": (-1000.0, 1000.0),
    "vibration_rms_g": (0.0, 20.0),
    "vibration_peak_g": (0.0, 50.0),
    "vibration_dominant_hz": (0.0, 10000.0),
    "soil_temperature_c": (-50.0, 100.0),
    "groundwater_depth_m": (0.0, 1000.0),
}


def _number(field: str, value: Any, low: float, high: float) -> None:
    if isinstance(value, bool) or not isinstance(value, Real):
        raise SchemaValidationError(f"{field}: must be a finite number")
    value = float(value)
    if not math.isfinite(value):
        raise SchemaValidationError(f"{field}: must be a finite number")
    if not low <= value <= high:
        raise SchemaValidationError(f"{field}: must be between {low:g} and {high:g}")


def validate_v2_fields(payload: Mapping[str, Any]) -> None:
    if "sensor_schema_version" not in payload:
        return
    version = payload["sensor_schema_version"]
    if isinstance(version, bool) or version != 2:
        raise SchemaValidationError("sensor_schema_version: only version 2 is supported")
    missing = [field for field in V2_FEATURE_RANGES if field not in payload]
    if missing:
        raise SchemaValidationError("payload: missing V2 feature(s): " + ", ".join(missing))
    for field, (low, high) in V2_FEATURE_RANGES.items():
        _number(field, payload[field], low, high)
    if abs(float(payload["soil_moisture_pct"]) - float(payload["soil_moisture_surface_pct"])) > 0.11:
        raise SchemaValidationError("soil_moisture_pct must mirror soil_moisture_surface_pct in V2")
    if abs(float(payload["tilt_deg"]) - abs(float(payload["tilt_x_deg"]))) > 0.11:
        raise SchemaValidationError("tilt_deg must mirror abs(tilt_x_deg) in V2")
    required_flags = (
        "model_ready", "is_simulated", "rainfall_history_valid",
        "groundwater_valid", "feature_inputs_valid",
    )
    missing_flags = [flag for flag in required_flags if flag not in payload]
    if missing_flags:
        raise SchemaValidationError("payload: missing V2 validity flag(s): " + ", ".join(missing_flags))
    for flag in required_flags:
        if not isinstance(payload[flag], bool):
            raise SchemaValidationError(f"{flag}: must be a boolean")
    if "tinyml_prediction" not in payload:
        raise SchemaValidationError("payload: missing tinyml_prediction")
    prediction = payload["tinyml_prediction"]
    if isinstance(prediction, bool) or not isinstance(prediction, int) or prediction not in {-1, 0, 1, 2}:
        raise SchemaValidationError("tinyml_prediction: must be -1, 0, 1, or 2")
    required_text = ("model_id", "measurement_provenance", "rainfall_history_mode")
    missing_text = [field for field in required_text if field not in payload]
    if missing_text:
        raise SchemaValidationError("payload: missing V2 provenance field(s): " + ", ".join(missing_text))
    for text_field in required_text:
        if not isinstance(payload[text_field], str) or not payload[text_field].strip():
            raise SchemaValidationError(f"{text_field}: must be a non-empty string")
    if payload["model_ready"] and not (payload["feature_inputs_valid"] and payload["rainfall_history_valid"] and payload["groundwater_valid"]):
        raise SchemaValidationError("model_ready cannot be true when required V2 feature history or sensor inputs are invalid")
    if not payload["model_ready"] and prediction != -1:
        raise SchemaValidationError("tinyml_prediction must be -1 when model_ready is false")
