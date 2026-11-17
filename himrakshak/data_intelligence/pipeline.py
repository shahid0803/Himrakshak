"""Reproducible, offline-safe intelligence pipeline command."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from hashlib import sha256
from pathlib import Path
from typing import Any

from .collectors.dem import process_dem
from .collectors.imd import import_imd_archive
from .collectors.landslides import import_csv, import_geojson, import_shapefile, inventory_metadata
from .registry import registry_as_dict
from .storage import IntelligenceStore


def _retrieved_at() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def ingest_landslide_file(path: str | Path, *, store: IntelligenceStore, **kwargs: Any) -> dict[str, Any]:
    suffix = Path(path).suffix.lower()
    importer = {".csv": import_csv, ".geojson": import_geojson, ".json": import_geojson, ".shp": import_shapefile}.get(suffix)
    if importer is None:
        raise ValueError("landslide input must be CSV, GeoJSON, or Shapefile")
    records = importer(path, **kwargs)
    store.insert_landslides(records)
    return inventory_metadata(records)


def ingest_rainfall_csv(path: str | Path, *, store: IntelligenceStore, longitude: float, latitude: float) -> int:
    records = import_imd_archive(path, longitude=longitude, latitude=latitude)
    if not isinstance(records, list):
        dataset = records.get("dataset") if isinstance(records, dict) else None
        close = getattr(dataset, "close", None)
        if callable(close):
            close()
        raise ValueError(
            "NetCDF was opened but not converted into observations; export the selected "
            "rainfall time series to CSV with observed_at and rainfall_mm columns first"
        )
    retrieved = _retrieved_at()
    observations = []
    for record in records:
        identity = f"{record['source']}|{record['source_product']}|{record['observed_at']}|{longitude}|{latitude}"
        observations.append({
            "observation_id": sha256(identity.encode()).hexdigest(),
            "station_id": None,
            "longitude": longitude,
            "latitude": latitude,
            "parameter_id": "rainfall_observation",
            "value": record["value"],
            "unit": record["unit"],
            "source": record["source"],
            "source_product": record["source_product"],
            "observed_at": record["observed_at"],
            "retrieved_at": retrieved,
            "quality_status": "missing" if record["value"] is None else "valid",
            "spatial_resolution": record.get("spatial_resolution"),
            "uncertainty": None,
            "processing_version": "18.3",
            "is_simulated": 0,
        })
    return store.insert_observations(observations)


def ingest_dem_file(path: str | Path, *, crs: str, store: IntelligenceStore | None = None) -> Any:
    features = process_dem(path, crs=crs)
    # GeoTIFF currently returns source metadata only; only ASCII-grid output
    # contains derived per-cell features suitable for terrain_features storage.
    if isinstance(features, list) and store is not None and features and "row" in features[0]:
        store.insert_terrain_features(features)
    return features


def run(task: str) -> dict[str, object]:
    if task not in {"rainfall", "terrain", "landslides", "all", "registry"}:
        raise ValueError(f"unsupported task: {task}")
    if task == "registry":
        status = "READY"
        message = "Parameter registry is available. No observations are implied by this status."
    else:
        status = "NO_SOURCE_DATA_IMPORTED"
        message = (
            "No external source data was imported by this command. Use a source-specific "
            "adapter with a real downloaded archive or configured source credentials. "
            "The research ML pipelines remain ineligible until genuine, time-aligned "
            "observations and event labels have been ingested."
        )
    return {
        "task": task,
        "status": status,
        "source_data_available": False,
        "model_training_enabled": False,
        "registry_version": registry_as_dict()["version"],
        "message": message,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="HimRakshak real-data intelligence pipeline")
    parser.add_argument("--task", choices=("rainfall", "terrain", "landslides", "all", "registry"), default="registry")
    parser.add_argument("--input", help="local source file for rainfall, terrain, or landslides")
    parser.add_argument("--db", default=":memory:", help="SQLite path for ingestion output")
    parser.add_argument("--longitude", type=float)
    parser.add_argument("--latitude", type=float)
    parser.add_argument("--crs", help="projected CRS for DEM input, e.g. EPSG:32643")
    args = parser.parse_args()
    if args.input:
        store = IntelligenceStore(args.db)
        if args.task == "landslides":
            result = ingest_landslide_file(args.input, store=store)
        elif args.task == "rainfall":
            if args.longitude is None or args.latitude is None:
                parser.error("--longitude and --latitude are required for rainfall input")
            result = {"records": ingest_rainfall_csv(args.input, store=store, longitude=args.longitude, latitude=args.latitude)}
        elif args.task == "terrain":
            if not args.crs:
                parser.error("--crs is required for terrain input")
            imported = ingest_dem_file(args.input, crs=args.crs, store=store)
            if isinstance(imported, list):
                result = {"features": len(imported), "status": "DERIVATIVES_PROCESSED"}
            else:
                result = {"features": 0, "status": imported.get("status", "METADATA_ONLY"), "metadata": imported}
        else:
            parser.error("--input must be used with rainfall, terrain, or landslides")
        print(json.dumps(result, indent=2))
        return
    print(json.dumps(run(args.task), indent=2))


if __name__ == "__main__":
    main()
