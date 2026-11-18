#!/usr/bin/env python3
"""Generate a reproducible synthetic HimRakshak TinyML demonstration dataset.

Labels are illustrative scenario labels, not observed landslide outcomes.
Never use this dataset as evidence of field prediction accuracy.
"""
from __future__ import annotations

import argparse
import csv
import random
from pathlib import Path

FEATURES = [
    "rainfall_intensity_mm_h", "rainfall_1h_mm", "rainfall_24h_mm",
    "soil_moisture_surface_pct", "soil_moisture_subsurface_pct",
    "pore_water_pressure_kpa", "tilt_x_deg", "tilt_y_deg",
    "tilt_rate_deg_per_h", "ground_displacement_mm",
    "ground_displacement_rate_mm_per_h", "vibration_rms_g",
    "vibration_peak_g", "vibration_dominant_hz", "soil_temperature_c",
    "groundwater_depth_m",
]


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def label_sample(x: dict[str, float]) -> int:
    """Transparent demonstration labelling rules for synthetic samples."""
    critical = (
        x["tilt_x_deg"] >= 8.5
        or x["ground_displacement_rate_mm_per_h"] >= 2.2
        or (x["pore_water_pressure_kpa"] >= 108 and x["soil_moisture_subsurface_pct"] >= 72)
        or (
            x["rainfall_24h_mm"] >= 145
            and x["soil_moisture_surface_pct"] >= 72
            and x["pore_water_pressure_kpa"] >= 82
        )
        or (
            x["tilt_rate_deg_per_h"] >= 1.8
            and x["pore_water_pressure_kpa"] >= 78
        )
    )
    if critical:
        return 2
    warning = (
        (x["rainfall_24h_mm"] >= 65 and x["soil_moisture_surface_pct"] >= 54)
        or x["pore_water_pressure_kpa"] >= 66
        or x["tilt_x_deg"] >= 4.1
        or x["ground_displacement_rate_mm_per_h"] >= 0.55
        or x["vibration_peak_g"] >= 0.95
        or x["groundwater_depth_m"] <= 1.8
        or x["rainfall_intensity_mm_h"] >= 35
    )
    return 1 if warning else 0


def sample_for_class(rng: random.Random, risk_class: int) -> dict[str, float | int]:
    """Generate one correlated virtual scenario with a declared demo class."""
    if risk_class == 0:
        rain_intensity = rng.uniform(0, 22)
        rain_1h = rng.uniform(0, 7)
        rain_24h = rng.uniform(0, 34)
        surface = rng.uniform(12, 58)
        subsurface = clamp(surface * rng.uniform(0.65, 1.0) + rng.gauss(0, 6), 8, 62)
        pore = clamp(subsurface * rng.uniform(0.3, 0.8) + rng.gauss(4, 8), 0, 58)
        tilt_x = rng.uniform(0, 3.6)
        tilt_y = clamp(tilt_x * rng.uniform(0.08, 0.4) + rng.gauss(0, 0.25), 0, 2)
        tilt_rate = rng.uniform(0, 0.35)
        displacement = clamp(tilt_x * rng.uniform(1.0, 2.7) + rng.uniform(0, 2), 0, 12)
        displacement_rate = rng.uniform(0, 0.35)
        vib_rms = rng.uniform(0.01, 0.18)
        vib_peak = rng.uniform(0.02, 0.48)
        groundwater = rng.uniform(2.5, 10.0)
    elif risk_class == 1:
        if rng.random() < 0.25:
            # A distinct vibration-anomaly warning subtype. Other features stay
            # near baseline so the model can learn the event input itself,
            # matching the Wokwi pushbutton demonstration. This is an
            # inspection/anomaly warning, not proof of slope failure.
            rain_intensity = rng.uniform(0, 22)
            rain_1h = rng.uniform(0, 7)
            rain_24h = rng.uniform(0, 34)
            surface = rng.uniform(12, 58)
            subsurface = clamp(surface * rng.uniform(0.65, 1.0) + rng.gauss(0, 6), 8, 62)
            pore = clamp(subsurface * rng.uniform(0.3, 0.8) + rng.gauss(4, 8), 0, 58)
            tilt_x = rng.uniform(0, 3.6)
            tilt_y = clamp(tilt_x * rng.uniform(0.08, 0.4) + rng.gauss(0, 0.25), 0, 2)
            tilt_rate = rng.uniform(0, 0.35)
            displacement = clamp(tilt_x * rng.uniform(1.0, 2.7) + rng.uniform(0, 2), 0, 12)
            displacement_rate = rng.uniform(0, 0.35)
            vib_rms = rng.uniform(0.32, 0.78)
            vib_peak = rng.uniform(0.98, 1.55)
            groundwater = rng.uniform(2.5, 10.0)
        else:
            rain_intensity = rng.uniform(12, 52)
            rain_1h = rng.uniform(5, 31)
            rain_24h = rng.uniform(25, 115)
            surface = rng.uniform(42, 82)
            subsurface = clamp(surface * rng.uniform(0.75, 1.08) + rng.gauss(2, 7), 35, 88)
            pore = clamp(subsurface * rng.uniform(0.65, 1.2) + rng.gauss(15, 12), 35, 112)
            tilt_x = rng.uniform(2.8, 7.8)
            tilt_y = clamp(tilt_x * rng.uniform(0.15, 0.55) + rng.gauss(0, 0.4), 0, 5)
            tilt_rate = rng.uniform(0.2, 1.7)
            displacement = clamp(tilt_x * rng.uniform(1.5, 3.4) + rng.uniform(1, 8), 5, 32)
            displacement_rate = rng.uniform(0.25, 1.85)
            vib_rms = rng.uniform(0.06, 0.62)
            vib_peak = rng.uniform(0.2, 1.35)
            groundwater = rng.uniform(0.9, 4.8)
    else:
        # Critical profiles deliberately cover several independent failure
        # indicators. In particular, displacement-rate critical cases include
        # moderate tilt/rain values similar to the HR-01 Wokwi slope scenario.
        subtype = rng.random()
        if subtype < 0.30:
            # Rapid displacement with otherwise moderate environmental values.
            rain_intensity = rng.uniform(0, 30)
            rain_1h = rng.uniform(0, 8)
            rain_24h = rng.uniform(0, 60)
            surface = rng.uniform(35, 70)
            subsurface = rng.uniform(35, 72)
            pore = rng.uniform(45, 82)
            tilt_x = rng.uniform(0.8, 5.8)
            tilt_y = rng.uniform(0, 2.5)
            tilt_rate = rng.uniform(0.1, 1.6)
            displacement = rng.uniform(3, 15)
            displacement_rate = rng.uniform(2.5, 8.5)
            vib_rms = rng.uniform(0.05, 0.65)
            vib_peak = rng.uniform(0.15, 1.5)
            groundwater = rng.uniform(1.9, 6.5)
        elif subtype < 0.55:
            # High pore pressure with elevated subsurface moisture.
            rain_intensity = rng.uniform(10, 60)
            rain_1h = rng.uniform(4, 35)
            rain_24h = rng.uniform(20, 150)
            surface = rng.uniform(55, 90)
            subsurface = rng.uniform(80, 100)
            pore = rng.uniform(118, 160)
            tilt_x = rng.uniform(1.5, 8.2)
            tilt_y = rng.uniform(0.1, 5.0)
            tilt_rate = rng.uniform(0.1, 1.7)
            displacement = rng.uniform(4, 24)
            displacement_rate = rng.uniform(0.1, 2.0)
            vib_rms = rng.uniform(0.05, 0.8)
            vib_peak = rng.uniform(0.2, 1.6)
            groundwater = rng.uniform(1.9, 4.8)
        elif subtype < 0.80:
            # Prolonged heavy rainfall combined with saturated surface soil.
            rain_intensity = rng.uniform(35, 80)
            rain_1h = rng.uniform(15, 70)
            rain_24h = rng.uniform(155, 270)
            surface = rng.uniform(78, 100)
            subsurface = rng.uniform(72, 100)
            pore = rng.uniform(84, 125)
            tilt_x = rng.uniform(3.0, 8.2)
            tilt_y = rng.uniform(0.2, 5.0)
            tilt_rate = rng.uniform(0.1, 1.7)
            displacement = rng.uniform(5, 30)
            displacement_rate = rng.uniform(0.1, 2.0)
            vib_rms = rng.uniform(0.08, 1.0)
            vib_peak = rng.uniform(0.2, 2.0)
            groundwater = rng.uniform(1.9, 3.5)
        else:
            # Rapid tilt-rate combined with elevated pore pressure.
            rain_intensity = rng.uniform(5, 55)
            rain_1h = rng.uniform(3, 30)
            rain_24h = rng.uniform(10, 140)
            surface = rng.uniform(45, 85)
            subsurface = rng.uniform(60, 95)
            pore = rng.uniform(85, 120)
            tilt_x = rng.uniform(0.5, 8.2)
            tilt_y = rng.uniform(0.1, 5.0)
            tilt_rate = rng.uniform(2.3, 8.0)
            displacement = rng.uniform(3, 25)
            displacement_rate = rng.uniform(0.1, 2.0)
            vib_rms = rng.uniform(0.05, 0.8)
            vib_peak = rng.uniform(0.2, 1.8)
            groundwater = rng.uniform(1.9, 5.5)
    # Add mild measurement noise; generate() assigns the final rule-based class.
    rain_intensity = clamp(rain_intensity + rng.gauss(0, 2), 0, 80)
    rain_1h = clamp(rain_1h + rng.gauss(0, 1.5), 0, 80)
    rain_24h = clamp(rain_24h + rng.gauss(0, 5), 0, 300)
    surface = clamp(surface + rng.gauss(0, 2), 0, 100)
    subsurface = clamp(subsurface + rng.gauss(0, 2), 0, 100)
    pore = clamp(pore + rng.gauss(0, 3), 0, 180)
    tilt_x = clamp(tilt_x + rng.gauss(0, 0.18), 0, 20)
    tilt_y = clamp(tilt_y + rng.gauss(0, 0.15), 0, 12)
    tilt_rate = clamp(tilt_rate + rng.gauss(0, 0.08), 0, 12)
    displacement = clamp(displacement + rng.gauss(0, 0.5), 0, 80)
    displacement_rate = clamp(displacement_rate + rng.gauss(0, 0.08), 0, 12)
    vib_rms = clamp(vib_rms + rng.gauss(0, 0.015), 0, 2)
    vib_peak = clamp(vib_peak + rng.gauss(0, 0.04), 0, 4)
    vib_freq = clamp(rng.uniform(0.5, 35), 0.5, 60)
    soil_temp = rng.uniform(-8, 28)
    groundwater = clamp(groundwater + rng.gauss(0, 0.15), 0.1, 12)
    return {
        "rainfall_intensity_mm_h": rain_intensity,
        "rainfall_1h_mm": rain_1h,
        "rainfall_24h_mm": rain_24h,
        "soil_moisture_surface_pct": surface,
        "soil_moisture_subsurface_pct": subsurface,
        "pore_water_pressure_kpa": pore,
        "tilt_x_deg": tilt_x,
        "tilt_y_deg": tilt_y,
        "tilt_rate_deg_per_h": tilt_rate,
        "ground_displacement_mm": displacement,
        "ground_displacement_rate_mm_per_h": displacement_rate,
        "vibration_rms_g": vib_rms,
        "vibration_peak_g": vib_peak,
        "vibration_dominant_hz": vib_freq,
        "soil_temperature_c": soil_temp,
        "groundwater_depth_m": groundwater,
        "risk_class": risk_class,
    }


def generate(path: Path, count: int = 240000, seed: int = 20261117) -> dict[str, int]:
    if count <= 0 or count % 3 != 0:
        raise ValueError("count must be a positive multiple of 3 for exact class balance")
    rng = random.Random(seed)
    rows: list[dict[str, float | int]] = []
    per_class = count // 3
    # Rejection-sample each class until its features satisfy the same explicit
    # demonstration rules used to define NORMAL/WARNING/CRITICAL labels.
    # This keeps the set exactly balanced without forcing a potentially
    # contradictory label onto a feature vector.
    for target_class in (0, 1, 2):
        accepted = 0
        attempts = 0
        max_attempts = per_class * 25
        while accepted < per_class:
            attempts += 1
            candidate = sample_for_class(rng, target_class)
            actual_class = label_sample(candidate)
            if actual_class == target_class:
                candidate["risk_class"] = actual_class
                rows.append(candidate)
                accepted += 1
            if attempts > max_attempts:
                raise RuntimeError(
                    f"Could not generate enough class {target_class} rows; "
                    f"accepted {accepted}/{per_class} after {attempts} attempts"
                )
    rng.shuffle(rows)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FEATURES + ["risk_class"])
        writer.writeheader()
        for row in rows:
            writer.writerow({key: round(float(row[key]), 5) if key != "risk_class" else row[key] for key in writer.fieldnames})
    return {str(label): sum(int(row["risk_class"]) == label for row in rows) for label in (0, 1, 2)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="data/processed/himrakshak_synthetic_v2.csv")
    parser.add_argument("--count", type=int, default=240000)
    parser.add_argument("--seed", type=int, default=20261117)
    args = parser.parse_args()
    counts = generate(Path(args.output), args.count, args.seed)
    print({"output": args.output, "rows": args.count, "class_counts": counts, "source": "synthetic_demo_only"})

if __name__ == "__main__":
    main()
