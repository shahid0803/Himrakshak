"""Explainable V2 multi-parameter assessment with local TinyML comparison.

All thresholds are illustrative demonstration settings, not site-calibrated
geotechnical safety limits. The embedded model was trained on synthetic labels.
"""
from __future__ import annotations

from typing import Any, Mapping

from .tinyml import CLASS_NAMES, predict_reading

MODEL_ID = "himrakshak-v2-hybrid-synthetic-demo"


def assess_risk_v2(reading: Mapping[str, Any]) -> dict[str, Any]:
    if not reading.get("feature_inputs_valid", True):
        return {
            "risk_level": "SENSOR_FAULT", "demo_score": 0,
            "reasons": ["One or more required environmental features are invalid"],
            "model_id": MODEL_ID, "illustrative": True,
            "tinyml_prediction": -1, "tinyml_label": "MODEL_INPUT_INVALID",
            "rule_based_level": "SENSOR_FAULT", "model_agreement": False,
        }
    model_ready = bool(reading.get("model_ready", True)) and bool(reading.get("rainfall_history_valid", True)) and bool(reading.get("groundwater_valid", True))
    device_prediction = reading.get("tinyml_prediction", -1)
    prediction = predict_reading(reading) if model_ready else -1
    critical_reasons: list[str] = []
    warning_reasons: list[str] = []
    if abs(float(reading["tilt_x_deg"])) >= 8.5:
        critical_reasons.append("X-axis tilt reached the illustrative critical threshold")
    if float(reading["ground_displacement_rate_mm_per_h"]) >= 2.2:
        critical_reasons.append("Ground-displacement proxy rate reached the critical threshold")
    if float(reading["pore_water_pressure_kpa"]) >= 108 and float(reading["soil_moisture_subsurface_pct"]) >= 72:
        critical_reasons.append("Elevated pore pressure combined with high subsurface moisture")
    if float(reading["rainfall_24h_mm"]) >= 145 and float(reading["soil_moisture_surface_pct"]) >= 72 and float(reading["pore_water_pressure_kpa"]) >= 82:
        critical_reasons.append("Combined 24-hour rainfall, surface moisture and pore pressure threshold")
    if float(reading["tilt_rate_deg_per_h"]) >= 1.8 and float(reading["pore_water_pressure_kpa"]) >= 78:
        critical_reasons.append("Increasing tilt rate coincides with elevated pore pressure")

    if float(reading["rainfall_24h_mm"]) >= 65 and float(reading["soil_moisture_surface_pct"]) >= 54:
        warning_reasons.append("Accumulated rainfall coincides with elevated surface moisture")
    if float(reading["pore_water_pressure_kpa"]) >= 66:
        warning_reasons.append("Pore-water pressure is elevated")
    if abs(float(reading["tilt_x_deg"])) >= 4.1:
        warning_reasons.append("Slope inclination proxy is elevated")
    if float(reading["ground_displacement_rate_mm_per_h"]) >= 0.55:
        warning_reasons.append("Ground-displacement proxy is changing")
    if float(reading["vibration_peak_g"]) >= 0.95:
        warning_reasons.append("Vibration event proxy is elevated")
    if float(reading["groundwater_depth_m"]) <= 1.8:
        warning_reasons.append("Groundwater is shallow in the virtual well profile")
    if float(reading["rainfall_intensity_mm_h"]) >= 35:
        warning_reasons.append("Rainfall intensity is elevated")

    rule_class = 2 if critical_reasons else (1 if warning_reasons else 0)
    final_class = max(rule_class, prediction if prediction >= 0 else 0)  # ML cannot suppress rule-based safety.
    reasons = critical_reasons + warning_reasons
    if not model_ready:
        reasons.append("TinyML inference is not ready because one or more history/input windows are unavailable")
    if prediction > rule_class:
        reasons.append("TinyML demonstration model suggested a higher class; conservative fusion retained it")
    if not reasons:
        reasons.append("No configured demonstration threshold was crossed")
    score = min(100, int(round(
        min(1.0, float(reading["rainfall_24h_mm"]) / 220) * 16
        + min(1.0, float(reading["soil_moisture_subsurface_pct"]) / 100) * 12
        + min(1.0, float(reading["pore_water_pressure_kpa"]) / 160) * 18
        + min(1.0, abs(float(reading["tilt_x_deg"])) / 15) * 18
        + min(1.0, float(reading["ground_displacement_rate_mm_per_h"]) / 8) * 18
        + min(1.0, float(reading["vibration_peak_g"]) / 3.5) * 8
        + (1.0 - min(1.0, float(reading["groundwater_depth_m"]) / 10)) * 10
    )))
    return {
        "risk_level": CLASS_NAMES[final_class],
        "demo_score": score,
        "reasons": reasons,
        "model_id": MODEL_ID,
        "illustrative": True,
        "tinyml_prediction": prediction,
        "server_tinyml_prediction": prediction,
        "device_tinyml_prediction": int(device_prediction) if isinstance(device_prediction, int) and not isinstance(device_prediction, bool) else -1,
        "tinyml_label": CLASS_NAMES[prediction] if prediction >= 0 else "MODEL_NOT_READY",
        "model_ready": model_ready,
        "rule_based_level": CLASS_NAMES[rule_class],
        "model_agreement": prediction == rule_class,
        "fusion_policy": "MAX_SEVERITY_RULES_AND_TINYML; RULES_CAN_RAISE_BUT_NEVER_BE_LOWERED_BY_ML",
        "features": 16,
    }
