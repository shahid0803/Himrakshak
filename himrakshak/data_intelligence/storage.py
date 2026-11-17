"""Small SQLite provenance store; raw rasters remain files outside the DB."""

from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any, Iterable, Mapping

SCHEMA = """
CREATE TABLE IF NOT EXISTS parameter_catalog (
  parameter_id TEXT PRIMARY KEY, definition_json TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS observations (
  observation_id TEXT PRIMARY KEY, station_id TEXT, longitude REAL NOT NULL,
  latitude REAL NOT NULL, parameter_id TEXT NOT NULL, value REAL,
  unit TEXT NOT NULL, source TEXT NOT NULL, source_product TEXT,
  observed_at TEXT NOT NULL, retrieved_at TEXT NOT NULL,
  quality_status TEXT NOT NULL, spatial_resolution TEXT, uncertainty REAL,
  processing_version TEXT NOT NULL, is_simulated INTEGER NOT NULL DEFAULT 0,
  UNIQUE(parameter_id, source, source_product, observed_at, longitude, latitude)
);
CREATE INDEX IF NOT EXISTS observations_parameter_time
  ON observations(parameter_id, observed_at);
CREATE TABLE IF NOT EXISTS landslide_inventory (
  event_id TEXT PRIMARY KEY, longitude REAL NOT NULL, latitude REAL NOT NULL,
  event_date TEXT, source TEXT NOT NULL, attributes_json TEXT NOT NULL,
  is_duplicate INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS terrain_features (
  feature_id TEXT PRIMARY KEY, x REAL NOT NULL, y REAL NOT NULL,
  crs TEXT NOT NULL, elevation REAL, slope_angle REAL, slope_aspect REAL,
  terrain_curvature REAL, flow_accumulation REAL, drainage_density REAL,
  source TEXT NOT NULL, source_product TEXT NOT NULL,
  spatial_resolution TEXT, quality_status TEXT NOT NULL, processing_version TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS soil_observations (
  observation_id TEXT PRIMARY KEY, longitude REAL NOT NULL, latitude REAL NOT NULL,
  property TEXT NOT NULL, depth TEXT, value REAL, unit TEXT, source TEXT NOT NULL,
  source_product TEXT NOT NULL, observed_at TEXT, retrieved_at TEXT NOT NULL,
  quality_status TEXT NOT NULL, uncertainty REAL, is_modelled INTEGER NOT NULL DEFAULT 1
);
CREATE TABLE IF NOT EXISTS model_registry (
  model_id TEXT PRIMARY KEY, model_type TEXT NOT NULL, status TEXT NOT NULL,
  metadata_json TEXT NOT NULL, created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS data_sources (
  source TEXT PRIMARY KEY, source_product TEXT, status TEXT NOT NULL,
  metadata_json TEXT NOT NULL, updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS ingestion_runs (
  run_id TEXT PRIMARY KEY, source TEXT NOT NULL, started_at TEXT NOT NULL,
  completed_at TEXT, status TEXT NOT NULL, start_time TEXT, end_time TEXT,
  coverage_json TEXT, records INTEGER NOT NULL DEFAULT 0,
  rejected_records INTEGER NOT NULL DEFAULT 0, missing_observations INTEGER NOT NULL DEFAULT 0,
  processing_version TEXT NOT NULL, error TEXT
);
"""


class IntelligenceStore:
    def __init__(self, path: str | Path = "data/himrakshak_intelligence.sqlite3"):
        self.path = Path(path)
        self._memory_connection: sqlite3.Connection | None = None
        if str(self.path) != ":memory:":
            self.path.parent.mkdir(parents=True, exist_ok=True)

    def connect(self) -> sqlite3.Connection:
        if str(self.path) == ":memory:":
            if self._memory_connection is None:
                self._memory_connection = sqlite3.connect(":memory:")
            connection = self._memory_connection
        else:
            connection = sqlite3.connect(str(self.path))
        connection.row_factory = sqlite3.Row
        connection.executescript(SCHEMA)
        terrain_columns = {
            row[1] for row in connection.execute("PRAGMA table_info(terrain_features)")
        }
        for name in ("flow_accumulation", "drainage_density"):
            if name not in terrain_columns:
                connection.execute(f"ALTER TABLE terrain_features ADD COLUMN {name} REAL")
        return connection

    def _close(self, connection: sqlite3.Connection) -> None:
        if connection is not self._memory_connection:
            connection.close()

    def insert_observations(self, observations: Iterable[Mapping[str, Any]]) -> int:
        rows = list(observations)
        connection = self.connect()
        try:
            with connection:
                connection.executescript(SCHEMA)
                connection.executemany(
                    """INSERT OR IGNORE INTO observations
                    (observation_id, station_id, longitude, latitude, parameter_id, value,
                     unit, source, source_product, observed_at, retrieved_at, quality_status,
                     spatial_resolution, uncertainty, processing_version, is_simulated)
                    VALUES (:observation_id, :station_id, :longitude, :latitude, :parameter_id,
                     :value, :unit, :source, :source_product, :observed_at, :retrieved_at,
                     :quality_status, :spatial_resolution, :uncertainty, :processing_version,
                     :is_simulated)""",
                    rows,
                )
        finally:
            self._close(connection)
        return len(rows)

    def latest(self, parameter_id: str | None = None) -> list[dict[str, Any]]:
        query = "SELECT * FROM observations"
        args: tuple[Any, ...] = ()
        if parameter_id:
            query += " WHERE parameter_id = ?"
            args = (parameter_id,)
        query += " ORDER BY observed_at DESC"
        connection = self.connect()
        try:
            return [dict(row) for row in connection.execute(query, args)]
        finally:
            self._close(connection)

    def table_rows(self, table: str, *, limit: int = 1000) -> list[dict[str, Any]]:
        allowed = {"landslide_inventory", "terrain_features", "soil_observations", "ingestion_runs", "data_sources"}
        if table not in allowed:
            raise ValueError("unsupported intelligence table")
        connection = self.connect()
        try:
            return [dict(row) for row in connection.execute(f"SELECT * FROM {table} LIMIT ?", (limit,))]
        finally:
            self._close(connection)

    def register_model(self, model_id: str, model_type: str, status: str, metadata: Mapping[str, Any], created_at: str) -> None:
        connection = self.connect()
        try:
            with connection:
                connection.execute(
                    "INSERT OR REPLACE INTO model_registry VALUES (?, ?, ?, ?, ?)",
                    (model_id, model_type, status, json.dumps(dict(metadata)), created_at),
                )
        finally:
            self._close(connection)

    def insert_landslides(self, records: Iterable[Mapping[str, Any]]) -> int:
        rows = list(records)
        connection = self.connect()
        try:
            with connection:
                connection.executemany(
                    """INSERT OR REPLACE INTO landslide_inventory
                    (event_id, longitude, latitude, event_date, source, attributes_json, is_duplicate)
                    VALUES (:event_id, :longitude, :latitude, :event_date, :source, :attributes_json, :is_duplicate)""",
                    (
                        {
                            **record,
                            "attributes_json": json.dumps(record.get("attributes", {}), default=str),
                            "is_duplicate": int(bool(record.get("is_duplicate"))),
                        }
                        for record in rows
                    ),
                )
        finally:
            self._close(connection)
        return len(rows)

    def record_ingestion_run(self, run: Mapping[str, Any]) -> None:
        connection = self.connect()
        try:
            with connection:
                connection.execute(
                    """INSERT OR REPLACE INTO ingestion_runs
                    (run_id, source, started_at, completed_at, status, start_time, end_time,
                     coverage_json, records, rejected_records, missing_observations,
                     processing_version, error)
                    VALUES (:run_id, :source, :started_at, :completed_at, :status, :start_time,
                     :end_time, :coverage_json, :records, :rejected_records,
                     :missing_observations, :processing_version, :error)""",
                    {**run, "coverage_json": json.dumps(run.get("coverage", {}))},
                )
        finally:
            self._close(connection)

    def record_source_status(self, source: str, status: str, metadata: Mapping[str, Any], updated_at: str, source_product: str | None = None) -> None:
        connection = self.connect()
        try:
            with connection:
                connection.execute(
                    "INSERT OR REPLACE INTO data_sources VALUES (?, ?, ?, ?, ?)",
                    (source, source_product, status, json.dumps(dict(metadata)), updated_at),
                )
        finally:
            self._close(connection)

    def insert_terrain_features(self, features: Iterable[Mapping[str, Any]], *, processing_version: str = "18.2") -> int:
        rows = list(features)
        connection = self.connect()
        try:
            with connection:
                connection.executemany(
                    """INSERT OR REPLACE INTO terrain_features
                    (feature_id, x, y, crs, elevation, slope_angle, slope_aspect,
                     terrain_curvature, flow_accumulation, drainage_density, source, source_product, spatial_resolution,
                     quality_status, processing_version)
                    VALUES (:feature_id, :x, :y, :crs, :elevation, :slope_angle,
                     :slope_aspect, :terrain_curvature, :flow_accumulation, :drainage_density,
                     :source, :source_product,
                     :spatial_resolution, :quality_status, :processing_version)""",
                    (
                        {
                            **feature,
                            "feature_id": str(feature.get("feature_id") or f"{feature['row']}:{feature['column']}"),
                            "x": feature.get("x", feature.get("longitude")),
                            "y": feature.get("y", feature.get("latitude")),
                            "quality_status": feature.get("quality_status", "valid"),
                            "processing_version": processing_version,
                        }
                        for feature in rows
                    ),
                )
        finally:
            self._close(connection)
        return len(rows)
