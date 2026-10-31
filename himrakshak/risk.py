"""Explainable demonstration risk assessment for HimRakshak readings."""

from __future__ import annotations

import math
from typing import Any, Mapping

from .schema import validate_reading


MODEL_ID = "himrakshak-demo-risk-engine-v1"


def assess_risk(reading: Mapping[str, Any]) -> dict[str, Any]:
    """Assess one complete sensor packet using the dashboard's demo rules.

    The input must satisfy the canonical sensor contract. It is validated
    through :func:`validate_reading`, copied, and never mutated. Derived
    metadata supplied in the packet is ignored.

    Returns exactly the public keys ``risk_level``, ``demo_score``,
    ``reasons``, ``model_id``, and ``illustrative``.
    """

    normalized = validate_reading(reading)
    moisture = float(normalized["soil_moisture_pct"])
    tilt = float(normalized["tilt_deg"])
    vibration = float(normalized["vibration_level"])

    raw_score = (
        max(0.0, (moisture - 45.0) / 55.0) * 35.0
        + max(0.0, (tilt - 2.0) / 10.0) * 45.0
        + max(0.0, (vibration - 25.0) / 75.0) * 20.0
    )
    score = min(100, max(0, math.floor(raw_score + 0.5)))

    reasons = []
    if moisture >= 60:
        reasons.append("Elevated simulated moisture")
    if tilt >= 4:
        reasons.append("Elevated simulated tilt")
    if vibration >= 65:
        reasons.append("Elevated simulated vibration")
    if score >= 25:
        reasons.append("Demonstration score reached the warning threshold")

    critical_tilt = tilt >= 8
    critical_combination = moisture >= 75 and tilt >= 4
    critical_score = score >= 55
    critical = critical_tilt or critical_combination or critical_score
    warning = bool(reasons)
    if critical:
        level = "CRITICAL"
        if critical_tilt:
            reasons.append("Tilt reached the critical demonstration threshold")
        if critical_combination:
            reasons.append(
                "Combined moisture and tilt met the critical demonstration rule"
            )
        if critical_score:
            reasons.append("Demonstration score reached the critical threshold")
    elif warning:
        level = "WARNING"
    else:
        level = "NORMAL"

    return {
        "risk_level": level,
        "demo_score": score,
        "reasons": reasons,
        "model_id": MODEL_ID,
        "illustrative": True,
    }
