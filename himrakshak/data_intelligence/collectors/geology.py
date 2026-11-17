"""Documented manual-import boundary for GSI Bhukosh layers."""

from pathlib import Path


def import_gsi_layer(path: str | Path):
    if not Path(path).is_file():
        raise FileNotFoundError(f"GSI layer does not exist: {path}")
    return {"path": str(path), "source": "Geological Survey of India", "status": "manual_import_pending"}
