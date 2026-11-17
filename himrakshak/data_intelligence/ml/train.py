"""Optional, honest model training entry points."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any, Iterable, Mapping

from .datasets import training_status


def train_model(rows: Iterable[Mapping[str, object]], *, model_type: str) -> dict[str, Any]:
    eligibility = training_status(rows, model_type=model_type)
    if eligibility["status"] != "ELIGIBLE":
        return {
            "model_type": model_type,
            "status": eligibility["status"],
            "metrics": {},
            "limitations": ["No genuine, non-simulated labelled dataset met the training threshold."],
            "trained_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        }
    try:
        import sklearn  # type: ignore
    except ImportError:
        return {
            "model_type": model_type,
            "status": "BLOCKED_MISSING_OPTIONAL_DEPENDENCY",
            "metrics": {},
            "limitations": ["Install the optional ML requirements before training."],
        }
    return {
        "model_type": model_type,
        "status": "READY_FOR_FEATURE_MATRIX",
        "metrics": {},
        "limitations": ["Feature matrix construction and spatial/chronological validation are required before fitting."],
        "sklearn_version": sklearn.__version__,
    }
