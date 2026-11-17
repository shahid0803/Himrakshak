"""Model training eligibility and simulation separation."""

from __future__ import annotations

from collections.abc import Iterable, Mapping

SUSCEPTIBILITY_FEATURES = (
    "slope_angle", "elevation", "terrain_curvature", "slope_aspect",
    "soil_clay_content", "soil_sand_content", "soil_silt_content",
    "soil_organic_carbon", "distance_from_river", "distance_from_road_cuts",
    "drainage_density", "ndvi",
)
EARLY_WARNING_FEATURES = (
    "rainfall_intensity_1h", "rainfall_accumulation_24h",
    "rainfall_accumulation_72h", "rainfall_antecedent_7d",
    "shallow_soil_moisture", "pore_water_pressure", "groundwater_depth",
    "displacement_velocity", "displacement_acceleration", "ground_crack_width",
    "ground_acceleration", "stream_level_rise_rate", "air_temperature",
    "snowmelt_estimate",
)


def eligible_rows(rows: Iterable[Mapping[str, object]], *, require_target: bool = True) -> list[dict[str, object]]:
    result = []
    for row in rows:
        if row.get("is_simulated"):
            continue
        if require_target and row.get("target") not in (0, 1, False, True):
            continue
        result.append(dict(row))
    return result


def training_status(rows: Iterable[Mapping[str, object]], *, model_type: str) -> dict[str, object]:
    eligible = eligible_rows(rows)
    targets = {int(bool(row["target"])) for row in eligible if row.get("target") in (0, 1, False, True)}
    minimum = 20 if model_type == "susceptibility" else 30
    if len(eligible) < minimum or targets != {0, 1}:
        return {
            "status": "NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS",
            "eligible_rows": len(eligible),
            "target_classes": sorted(targets),
            "required_rows": minimum,
        }
    return {"status": "ELIGIBLE", "eligible_rows": len(eligible), "target_classes": sorted(targets)}
