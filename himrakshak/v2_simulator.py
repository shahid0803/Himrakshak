"""Deterministic V2 parameter-group scenarios for the judge demonstration."""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from .tinyml import predict_reading

V2_SCENARIOS = {
    "normal": {
        "rainfall_intensity_mm_h": 8.0, "rainfall_1h_mm": 3.0, "rainfall_24h_mm": 30.0,
        "soil_moisture_surface_pct": 32.0, "soil_moisture_subsurface_pct": 36.0,
        "pore_water_pressure_kpa": 28.0, "tilt_x_deg": 1.2, "tilt_y_deg": 0.2,
        "tilt_rate_deg_per_h": 0.05, "ground_displacement_mm": 2.0,
        "ground_displacement_rate_mm_per_h": 0.05, "vibration_rms_g": 0.02,
        "vibration_peak_g": 0.04, "vibration_dominant_hz": 1.0,
        "soil_temperature_c": 8.0, "groundwater_depth_m": 6.8,
    },
    "heavy_rain": {
        # Calibrated as a labelled demonstration vector so both the synthetic
        # tree and the independent illustrative rules identify WARNING.
        "rainfall_intensity_mm_h": 46.73, "rainfall_1h_mm": 14.721, "rainfall_24h_mm": 103.553,
        "soil_moisture_surface_pct": 70.496, "soil_moisture_subsurface_pct": 55.384,
        "pore_water_pressure_kpa": 72.808, "tilt_x_deg": 5.479, "tilt_y_deg": 2.372,
        "tilt_rate_deg_per_h": 0.608, "ground_displacement_mm": 16.748,
        "ground_displacement_rate_mm_per_h": 0.928, "vibration_rms_g": 0.44,
        "vibration_peak_g": 0.715, "vibration_dominant_hz": 5.905,
        "soil_temperature_c": 11.293, "groundwater_depth_m": 2.686,
    },
    "slope_shift": {
        "rainfall_intensity_mm_h": 35.0, "rainfall_1h_mm": 35.0, "rainfall_24h_mm": 190.0,
        "soil_moisture_surface_pct": 82.0, "soil_moisture_subsurface_pct": 88.0,
        "pore_water_pressure_kpa": 125.0, "tilt_x_deg": 9.0, "tilt_y_deg": 3.0,
        "tilt_rate_deg_per_h": 2.8, "ground_displacement_mm": 26.0,
        "ground_displacement_rate_mm_per_h": 3.4, "vibration_rms_g": 0.48,
        "vibration_peak_g": 1.05, "vibration_dominant_hz": 12.0,
        "soil_temperature_c": 4.0, "groundwater_depth_m": 1.2,
    },
    "vibration_burst": {
        # Isolated virtual vibration-event warning over otherwise baseline
        # geotechnical inputs; the button injects a test event, not field data.
        "rainfall_intensity_mm_h": 8.0, "rainfall_1h_mm": 3.0, "rainfall_24h_mm": 30.0,
        "soil_moisture_surface_pct": 32.0, "soil_moisture_subsurface_pct": 36.0,
        "pore_water_pressure_kpa": 28.0, "tilt_x_deg": 1.2, "tilt_y_deg": 0.2,
        "tilt_rate_deg_per_h": 0.05, "ground_displacement_mm": 2.0,
        "ground_displacement_rate_mm_per_h": 0.05, "vibration_rms_g": 0.55,
        "vibration_peak_g": 1.2, "vibration_dominant_hz": 12.0,
        "soil_temperature_c": 8.0, "groundwater_depth_m": 6.8,
    },}


def make_v2_packet(node_id: str, sequence: int, scenario: str) -> dict[str, Any]:
    if scenario not in V2_SCENARIOS:
        raise ValueError(f"unknown V2 scenario {scenario!r}; choose from {', '.join(V2_SCENARIOS)}")
    features = dict(V2_SCENARIOS[scenario])
    packet: dict[str, Any] = {
        "node_id": node_id,
        "sequence": sequence,
        "timestamp": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "soil_moisture_pct": features["soil_moisture_surface_pct"],
        "tilt_deg": abs(features["tilt_x_deg"]),
        "vibration_level": 85 if features["vibration_peak_g"] >= 0.95 else 12,
        "battery_pct": 88.0,
        "sensor_schema_version": 2,
        **features,
        "feature_inputs_valid": True,
        "rainfall_history_valid": True,
        "groundwater_valid": True,
        "model_ready": True,
        "tinyml_prediction": -1,
        "model_id": "himrakshak-synthetic-demo-tree-v2",
        "measurement_provenance": "PYTHON_SYNTHETIC_SCENARIO_DEMO",
        "rainfall_history_mode": "SYNTHETIC_SCENARIO_PROFILE",
        "is_simulated": True,
        "transport": "PYTHON_SIMULATOR",
        "scenario": scenario,
    }
    packet["tinyml_prediction"] = predict_reading(packet)
    return packet
