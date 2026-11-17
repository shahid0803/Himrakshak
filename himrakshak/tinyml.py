"""Portable inference runtime for the exported HimRakshak V2 demo tree.

The model was trained on synthetic scenario labels. It demonstrates embedded
inference parity; it is not a validated landslide-prediction model.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from numbers import Real
from typing import Mapping, Sequence

FEATURES = (
    "rainfall_intensity_mm_h", "rainfall_1h_mm", "rainfall_24h_mm",
    "soil_moisture_surface_pct", "soil_moisture_subsurface_pct",
    "pore_water_pressure_kpa", "tilt_x_deg", "tilt_y_deg",
    "tilt_rate_deg_per_h", "ground_displacement_mm",
    "ground_displacement_rate_mm_per_h", "vibration_rms_g",
    "vibration_peak_g", "vibration_dominant_hz", "soil_temperature_c",
    "groundwater_depth_m",
)
CLASS_NAMES = {0: "NORMAL", 1: "WARNING", 2: "CRITICAL"}
DEFAULT_MODEL_PATH = Path(__file__).resolve().parent.parent / "models" / "himrakshak_tinyml_v2.json"


def load_model(path: str | Path = DEFAULT_MODEL_PATH) -> dict:
    model = json.loads(Path(path).read_text(encoding="utf-8"))
    if tuple(model.get("feature_names", ())) != FEATURES:
        raise ValueError("TinyML model feature order does not match the runtime contract")
    if model.get("model_version") != "2.0.0-demo-synthetic":
        raise ValueError("Unsupported TinyML model version")
    supplied_hash = model.get("model_sha256")
    if not isinstance(supplied_hash, str) or len(supplied_hash) != 64:
        raise ValueError("TinyML model is missing a valid SHA-256 manifest")
    unsigned_model = dict(model)
    unsigned_model.pop("model_sha256", None)
    expected_hash = hashlib.sha256(
        json.dumps(unsigned_model, indent=2, sort_keys=True).encode("utf-8")
    ).hexdigest()
    if supplied_hash != expected_hash:
        raise ValueError("TinyML model SHA-256 verification failed")
    tree = model.get("tree")
    if not isinstance(tree, dict):
        raise ValueError("TinyML model tree is missing")
    required_arrays = ("feature", "threshold", "children_left", "children_right", "class")
    lengths = [len(tree.get(name, [])) for name in required_arrays]
    if not lengths[0] or len(set(lengths)) != 1:
        raise ValueError("TinyML model tree arrays are incomplete or mismatched")
    node_count = lengths[0]
    for index in range(node_count):
        feature_index = int(tree["feature"][index])
        if feature_index >= len(FEATURES):
            raise ValueError("TinyML model references an unknown feature index")
        if int(tree["class"][index]) not in CLASS_NAMES:
            raise ValueError("TinyML model contains an invalid class")
        if feature_index >= 0:
            left = int(tree["children_left"][index])
            right = int(tree["children_right"][index])
            if not (0 <= left < node_count and 0 <= right < node_count):
                raise ValueError("TinyML model contains an invalid child index")
    return model


def predict_features(values: Sequence[float], model: dict | None = None) -> int:
    if len(values) != len(FEATURES):
        raise ValueError(f"expected {len(FEATURES)} features, received {len(values)}")
    for index, value in enumerate(values):
        if isinstance(value, bool) or not isinstance(value, Real) or not math.isfinite(float(value)):
            raise ValueError(f"feature {FEATURES[index]} must be a finite number")
    current = load_model() if model is None else model
    node = 0
    features = current["tree"]["feature"]
    thresholds = current["tree"]["threshold"]
    left = current["tree"]["children_left"]
    right = current["tree"]["children_right"]
    classes = current["tree"]["class"]
    steps = 0
    while features[node] >= 0:
        index = int(features[node])
        node = int(left[node] if float(values[index]) <= float(thresholds[node]) else right[node])
        steps += 1
        if steps > len(features):
            raise ValueError("invalid/cyclic decision-tree model")
    result = int(classes[node])
    if result not in CLASS_NAMES:
        raise ValueError(f"invalid model class: {result}")
    return result


def predict_reading(reading: Mapping[str, object], model: dict | None = None) -> int:
    values = [float(reading[name]) for name in FEATURES]
    return predict_features(values, model)
