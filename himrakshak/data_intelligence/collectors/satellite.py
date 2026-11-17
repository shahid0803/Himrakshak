"""Offline-safe Sentinel discovery boundary.

This module intentionally does not claim to download imagery. It can inspect a
local STAC Item/FeatureCollection export and report its assets for a separate
processing workflow.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def discover_sentinel(*, stac_export: str | Path | None = None, **kwargs: Any) -> dict[str, Any]:
    if stac_export is None:
        return {
            "status": "SOURCE_CONFIGURATION_REQUIRED",
            "message": "Provide a local STAC Item/FeatureCollection export or configure an authenticated STAC client.",
            "items": [],
        }
    path = Path(stac_export)
    if not path.is_file():
        raise FileNotFoundError(f"STAC export does not exist: {path}")
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("type") == "FeatureCollection":
        items = payload.get("features", [])
    elif payload.get("type") == "Feature" or "assets" in payload:
        items = [payload]
    else:
        raise ValueError("STAC input must be an Item or FeatureCollection JSON export")
    result = []
    for item in items:
        assets = item.get("assets") or {}
        result.append({
            "id": item.get("id"),
            "datetime": (item.get("properties") or {}).get("datetime"),
            "collection": item.get("collection"),
            "asset_keys": sorted(assets),
            "assets": {key: value.get("href") for key, value in assets.items()},
        })
    return {"status": "LOCAL_STAC_EXPORT_PARSED", "items": result, "count": len(result)}
