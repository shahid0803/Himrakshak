"""Independent virtual station state and deterministic simulation management."""

from __future__ import annotations

from collections import deque
from copy import deepcopy
from dataclasses import dataclass
from datetime import datetime, timezone
import threading
import math
from typing import Any, Mapping

from .risk import assess_risk
from .reliability import MAX_PENDING_PACKETS, PendingPacket, ReliabilityStats
from .schema import SchemaValidationError, validate_reading
from .simulator import SCENARIOS, SensorSimulator


STATION_ROSTER = (
    ("HR-01", "Virtual Station 1"),
    ("HR-02", "Virtual Station 2"),
    ("HR-03", "Virtual Station 3"),
)
MAX_EVENTS = 100

REMOTE_SCENARIOS = {
    "normal", "heavy_rain", "slope_movement", "slope_shift",
    "vibration_burst", "groundwater_change", "sensor_failure",
}
REMOTE_SENSOR_DEFAULTS = {
    "rainfall_intensity_mm_h": 8.0,
    "surface_moisture_pct": 32.0,
    "subsurface_moisture_pct": 36.0,
    "pore_pressure_kpa": 28.0,
    "tilt_x_deg": 1.2,
    "tilt_y_deg": 0.2,
    "ground_displacement_mm": 2.0,
    "vibration_rms_g": 0.02,
    "vibration_frequency_hz": 8.0,
    "soil_temperature_c": 17.0,
    "groundwater_depth_m": 6.8,
    "air_temperature_c": 19.0,
    "relative_humidity_pct": 56.0,
    "atmospheric_pressure_hpa": 1012.0,
}
REMOTE_SENSOR_RANGES = {
    "rainfall_intensity_mm_h": (0.0, 200.0),
    "surface_moisture_pct": (0.0, 100.0),
    "subsurface_moisture_pct": (0.0, 100.0),
    "pore_pressure_kpa": (0.0, 160.0),
    "tilt_x_deg": (-15.0, 15.0),
    "tilt_y_deg": (-15.0, 15.0),
    "ground_displacement_mm": (0.0, 25.0),
    "vibration_rms_g": (0.0, 1.5),
    "vibration_frequency_hz": (0.0, 20.0),
    "soil_temperature_c": (-10.0, 60.0),
    "groundwater_depth_m": (0.2, 20.0),
    "air_temperature_c": (-20.0, 60.0),
    "relative_humidity_pct": (0.0, 100.0),
    "atmospheric_pressure_hpa": (800.0, 1100.0),
}


def validate_simulation_command(payload: Mapping[str, Any]) -> dict[str, Any]:
    """Normalize and validate a versioned command for a running Wokwi sender."""
    if not isinstance(payload, Mapping):
        raise ValueError("command payload must be an object")
    mode = payload.get("mode", "scenario")
    if mode not in {"scenario", "custom"}:
        raise ValueError("mode must be 'scenario' or 'custom'")
    scenario = payload.get("scenario", "normal")
    if not isinstance(scenario, str):
        raise ValueError("scenario must be a string")
    scenario = scenario.strip().lower().replace("-", "_")
    if scenario == "slope_shift":
        scenario = "slope_movement"
    if scenario not in REMOTE_SCENARIOS:
        raise ValueError("unsupported scenario")

    command: dict[str, Any] = {"mode": mode, "scenario": scenario}
    if mode == "custom":
        supplied = payload.get("sensor_values", payload)
        if not isinstance(supplied, Mapping):
            raise ValueError("sensor_values must be an object")
        values = dict(REMOTE_SENSOR_DEFAULTS)
        for field, (low, high) in REMOTE_SENSOR_RANGES.items():
            if field in supplied:
                value = supplied[field]
                if isinstance(value, bool) or not isinstance(value, (int, float)):
                    raise ValueError(f"{field} must be a finite number")
                value = float(value)
                if not math.isfinite(value) or not low <= value <= high:
                    raise ValueError(f"{field} must be between {low:g} and {high:g}")
                values[field] = value
        command["sensor_values"] = values
    return command


@dataclass(frozen=True)
class StationConfig:
    node_id: str
    label: str
    index: int


class StationState:
    """Mutable state for one independently simulated station."""

    def __init__(self, config: StationConfig, seed: int | None):
        station_seed = None if seed is None else seed + config.index
        self.config = config
        self.lock = threading.RLock()
        self.simulator = SensorSimulator(node_id=config.node_id, seed=station_seed)
        self.scenario = "normal"
        self.receiver_connected = True
        self.latest: dict[str, Any] | None = None
        self.events: deque[dict[str, Any]] = deque(maxlen=MAX_EVENTS)
        self.reliability = ReliabilityStats()
        self.pending: deque[PendingPacket] = deque(maxlen=MAX_PENDING_PACKETS)
        self._pending_order = 0
        self.simulation_command_version = 0
        self.simulation_command: dict[str, Any] | None = None
        self.simulation_command_acked_version = 0
        self.simulation_command_issued_at: str | None = None
        self.simulation_command_acknowledged_at: str | None = None
        self.device_last_seen_at: str | None = None
        self.device_id: str | None = None

    def simulation_control_status(self) -> dict[str, Any]:
        now = datetime.now(timezone.utc)
        last_seen = None
        online = False
        if self.device_last_seen_at:
            try:
                last_seen = datetime.fromisoformat(self.device_last_seen_at.replace("Z", "+00:00"))
                online = (now - last_seen).total_seconds() <= 20.0
            except ValueError:
                online = False
        return {
            "command_version": self.simulation_command_version,
            "acknowledged_version": self.simulation_command_acked_version,
            "pending": self.simulation_command_version > self.simulation_command_acked_version,
            "issued_at": self.simulation_command_issued_at,
            "acknowledged_at": self.simulation_command_acknowledged_at,
            "device_last_seen_at": self.device_last_seen_at,
            "device_online": online,
            "device_id": self.device_id,
            "mode": self.simulation_command.get("mode") if self.simulation_command else "scenario",
            "scenario": self.simulation_command.get("scenario") if self.simulation_command else self.scenario,
        }

    @property
    def next_sequence(self) -> int:
        return self.simulator.sequence

    def summary(self) -> dict[str, Any]:
        with self.lock:
            return {
                "node_id": self.config.node_id,
                "label": self.config.label,
                "scenario": self.scenario,
                "receiver_connected": self.receiver_connected,
                "next_sequence": self.next_sequence,
                "latest": deepcopy(self.latest),
                "simulation_control": self.simulation_control_status(),
                "event_count": len(self.events),
                "communication": self.reliability.as_dict(
                    len(self.pending), self.receiver_connected
                ),
            }


class StationManager:
    """Own the fixed roster, station state, and bounded global event history."""

    def __init__(self, seed: int | None = None):
        self.lock = threading.RLock()
        self.stations = {
            node_id: StationState(StationConfig(node_id, label, index), seed)
            for index, (node_id, label) in enumerate(STATION_ROSTER)
        }
        self.events: deque[dict[str, Any]] = deque(maxlen=MAX_EVENTS)

    def get(self, node_id: str) -> StationState:
        try:
            return self.stations[node_id]
        except KeyError as exc:
            raise KeyError(f"unknown station {node_id!r}") from exc

    def set_simulation_command(self, node_id: str, payload: Mapping[str, Any]) -> dict[str, Any]:
        station = self.get(node_id)
        command = validate_simulation_command(payload)
        with station.lock:
            station.simulation_command_version += 1
            station.simulation_command = deepcopy(command)
            station.simulation_command_issued_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
            station.scenario = command["scenario"]
            return {
                "station_id": node_id,
                "version": station.simulation_command_version,
                "command": deepcopy(command),
                "simulation_control": station.simulation_control_status(),
            }

    def get_simulation_command(self, node_id: str, device_id: str | None = None) -> dict[str, Any]:
        station = self.get(node_id)
        now_text = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        with station.lock:
            station.device_last_seen_at = now_text
            if device_id:
                station.device_id = str(device_id)[:80]
            command = deepcopy(station.simulation_command) if station.simulation_command else {
                "mode": "scenario", "scenario": "normal"
            }
            response: dict[str, Any] = {
                "station_id": node_id,
                "version": station.simulation_command_version,
                "acknowledged_version": station.simulation_command_acked_version,
                "has_command": station.simulation_command_version > 0,
                "pending": station.simulation_command_version > station.simulation_command_acked_version,
                "issued_at": station.simulation_command_issued_at,
                **command,
            }
            if command.get("mode") == "custom":
                response.update(deepcopy(command.get("sensor_values", {})))
            return response

    def acknowledge_simulation_command(
        self, node_id: str, payload: Mapping[str, Any]
    ) -> dict[str, Any]:
        station = self.get(node_id)
        version = payload.get("version") if isinstance(payload, Mapping) else None
        if isinstance(version, bool) or not isinstance(version, int) or version < 0:
            raise ValueError("version must be a non-negative integer")
        with station.lock:
            if version > station.simulation_command_version:
                raise ValueError("cannot acknowledge a command version that has not been issued")
            if version > station.simulation_command_acked_version:
                station.simulation_command_acked_version = version
                station.simulation_command_acknowledged_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
            device_id = payload.get("device_id") if isinstance(payload, Mapping) else None
            if isinstance(device_id, str) and device_id.strip():
                station.device_id = device_id.strip()[:80]
            return {
                "station_id": node_id,
                "acknowledged_version": station.simulation_command_acked_version,
                "simulation_control": station.simulation_control_status(),
            }

    def _event(
        self,
        station: StationState,
        event_type: str,
        result: dict[str, Any],
    ) -> dict[str, Any]:
        return {
            "station_id": station.config.node_id,
            "station_label": station.config.label,
            "timestamp": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "event": event_type,
            "reading": deepcopy(result["reading"]),
            "assessment": deepcopy(result["assessment"]),
        }

    def _accept(
        self,
        station: StationState,
        reading: dict[str, Any],
        assessment: dict[str, Any],
        event_type: str,
    ) -> dict[str, Any]:
        result = {"reading": deepcopy(reading), "assessment": deepcopy(assessment)}
        event = self._event(station, event_type, result)
        with self.lock, station.lock:
            station.latest = deepcopy(result)
            station.events.append(deepcopy(event))
            self.events.append(deepcopy(event))
            station.reliability.packets_delivered += 1
            station.reliability.last_outcome = "delivered"
            station.reliability.last_delivered_sequence = reading["sequence"]
        return result

    def simulate(self, node_id: str, scenario: str) -> dict[str, Any]:
        station = self.get(node_id)
        if scenario not in SCENARIOS:
            choices = ", ".join(sorted(SCENARIOS))
            raise ValueError(f"unknown scenario {scenario!r}; choose from {choices}")
        with self.lock, station.lock:
            if not station.receiver_connected:
                station.reliability.packets_attempted += 1
                station.reliability.disconnected_attempts += 1
                station.reliability.last_outcome = "disconnected"
                raise StationDisconnectedError(
                    f"station {node_id!r} is disconnected"
                )
            station.scenario = scenario
            station.simulator.scenario = scenario
            station.reliability.packets_attempted += 1
            reading = station.simulator.next_packet()
            assessment = assess_risk(reading)
        return self._accept(station, reading, assessment, f"simulate {scenario}")

    def ingest(
        self,
        node_id: str,
        payload: Mapping[str, Any],
        event_type: str = "api reading",
    ) -> dict[str, Any]:
        station = self.get(node_id)
        with self.lock, station.lock:
            if not station.receiver_connected:
                station.reliability.packets_attempted += 1
                station.reliability.disconnected_attempts += 1
                station.reliability.last_outcome = "disconnected"
                raise StationDisconnectedError(
                    f"station {node_id!r} is disconnected"
                )
        normalized = validate_reading(payload)
        if normalized["node_id"] != node_id:
            raise SchemaValidationError(
                f"node_id: must match station {node_id!r}"
            )
        with self.lock, station.lock:
            station.reliability.packets_attempted += 1
            classification = self._classify_sequence(
                station, normalized["sequence"]
            )
            if classification == "duplicate":
                station.reliability.duplicate_rejections += 1
                station.reliability.last_outcome = "duplicate"
                latest_seq = (
                    station.latest["reading"]["sequence"]
                    if station.latest
                    else None
                )
                raise StationSequenceConflictError(
                    f"duplicate packet sequence {normalized['sequence']} for station {node_id!r} (latest accepted: {latest_seq})",
                    classification="duplicate",
                    sequence=normalized["sequence"],
                    latest_sequence=latest_seq,
                )
            if classification == "stale":
                station.reliability.stale_rejections += 1
                station.reliability.last_outcome = "stale"
                latest_seq = (
                    station.latest["reading"]["sequence"]
                    if station.latest
                    else None
                )
                raise StationSequenceConflictError(
                    f"stale packet sequence {normalized['sequence']} for station {node_id!r} (latest accepted: {latest_seq})",
                    classification="stale",
                    sequence=normalized["sequence"],
                    latest_sequence=latest_seq,
                )
            assessment = assess_risk(normalized)
            station.simulator._sequence = max(
                station.simulator.sequence, normalized["sequence"] + 1
            )
        return self._accept(station, normalized, assessment, event_type)

    def station_details(self, node_id: str) -> dict[str, Any]:
        station = self.get(node_id)
        with station.lock:
            return {
                "node_id": station.config.node_id,
                "label": station.config.label,
                "scenario": station.scenario,
                "receiver_connected": station.receiver_connected,
                "next_sequence": station.next_sequence,
                "latest": deepcopy(station.latest),
                "simulation_control": station.simulation_control_status(),
                "events": deepcopy(list(station.events)),
                "event_count": len(station.events),
                "pending": [deepcopy(packet.reading) for packet in station.pending],
                "communication": station.reliability.as_dict(
                    len(station.pending), station.receiver_connected
                ),
            }

    def overview(self) -> dict[str, Any]:
        return {
            "stations": [self.stations[node_id].summary() for node_id, _ in STATION_ROSTER],
            "events": deepcopy(list(self.events)),
        }

    def legacy_state(self) -> dict[str, Any]:
        station = self.get("HR-01")
        with station.lock:
            return {
                "latest": deepcopy(station.latest),
                "events": deepcopy(list(self.events)),
            }

    def _prepare_reliability_reading(
        self, station: StationState, payload: Mapping[str, Any]
    ) -> dict[str, Any]:
        normalized = validate_reading(payload)
        if normalized["node_id"] != station.config.node_id:
            raise SchemaValidationError(
                f"node_id: must match station {station.config.node_id!r}"
            )
        return normalized

    def _reliability_status(self, station: StationState) -> dict[str, Any]:
        return station.reliability.as_dict(
            len(station.pending), station.receiver_connected
        )

    def drop_packet(
        self, node_id: str, payload: Mapping[str, Any]
    ) -> dict[str, Any]:
        station = self.get(node_id)
        with self.lock, station.lock:
            normalized = self._prepare_reliability_reading(station, payload)
            station.reliability.packets_attempted += 1
            station.reliability.packets_dropped += 1
            station.reliability.last_outcome = "dropped"
            return {
                "outcome": "dropped",
                "reading": normalized,
                "communication": self._reliability_status(station),
            }

    def delay_packet(
        self, node_id: str, payload: Mapping[str, Any]
    ) -> dict[str, Any]:
        station = self.get(node_id)
        with self.lock, station.lock:
            normalized = self._prepare_reliability_reading(station, payload)
            station.reliability.packets_attempted += 1
            if not station.receiver_connected:
                station.reliability.disconnected_attempts += 1
                station.reliability.last_outcome = "disconnected"
                raise StationDisconnectedError(
                    f"station {node_id!r} is disconnected"
                )
            if len(station.pending) >= MAX_PENDING_PACKETS:
                station.reliability.last_outcome = "queue_full"
                raise PendingQueueFullError(
                    f"pending packet queue is full (maximum {MAX_PENDING_PACKETS})"
                )
            station._pending_order += 1
            station.pending.append(
                PendingPacket(deepcopy(normalized), station._pending_order)
            )
            station.reliability.packets_delayed += 1
            station.reliability.last_outcome = "pending"
            return {
                "outcome": "pending",
                "reading": normalized,
                "communication": self._reliability_status(station),
            }

    def _classify_sequence(
        self, station: StationState, sequence: int
    ) -> str | None:
        if station.latest is None:
            return None
        latest_sequence = station.latest["reading"]["sequence"]
        if sequence == latest_sequence:
            return "duplicate"
        if sequence < latest_sequence:
            return "stale"
        return None

    def deliver_packet(
        self,
        node_id: str,
        payload: Mapping[str, Any],
        event_type: str = "reliability reading",
        count_attempt: bool = True,
    ) -> dict[str, Any]:
        station = self.get(node_id)
        with self.lock, station.lock:
            normalized = self._prepare_reliability_reading(station, payload)
            if count_attempt:
                station.reliability.packets_attempted += 1
            if not station.receiver_connected:
                station.reliability.disconnected_attempts += 1
                station.reliability.last_outcome = "disconnected"
                raise StationDisconnectedError(
                    f"station {node_id!r} is disconnected"
                )
            classification = self._classify_sequence(
                station, normalized["sequence"]
            )
            if classification == "duplicate":
                station.reliability.duplicate_rejections += 1
                station.reliability.last_outcome = "duplicate"
                return {
                    "outcome": "duplicate",
                    "reading": normalized,
                    "communication": self._reliability_status(station),
                }
            if classification == "stale":
                station.reliability.stale_rejections += 1
                station.reliability.last_outcome = "stale"
                return {
                    "outcome": "stale",
                    "reading": normalized,
                    "communication": self._reliability_status(station),
                }
            assessment = assess_risk(normalized)
            station.simulator._sequence = max(
                station.simulator.sequence, normalized["sequence"] + 1
            )
        result = self._accept(station, normalized, assessment, event_type)
        result["outcome"] = "delivered"
        result["communication"] = self._reliability_status(station)
        return result

    def release_packet(self, node_id: str) -> dict[str, Any]:
        station = self.get(node_id)
        with self.lock, station.lock:
            if not station.pending:
                raise PendingQueueEmptyError("no delayed packets are pending")
            packet = station.pending.popleft()
        result = self.deliver_packet(
            node_id, packet.reading, "released delayed packet", count_attempt=False
        )
        result["released_order"] = packet.queued_order
        return result

    def disconnect(self, node_id: str) -> dict[str, Any]:
        station = self.get(node_id)
        with self.lock, station.lock:
            station.receiver_connected = False
            station.reliability.last_outcome = "disconnected"
            return {
                "outcome": "disconnected",
                "communication": self._reliability_status(station),
            }

    def reconnect(self, node_id: str) -> dict[str, Any]:
        station = self.get(node_id)
        with self.lock, station.lock:
            station.receiver_connected = True
            station.reliability.last_outcome = "reconnected"
            return {
                "outcome": "reconnected",
                "communication": self._reliability_status(station),
            }


class StationReliabilityError(ValueError):
    """Base class for deterministic communication simulation errors."""


class StationSequenceConflictError(StationReliabilityError):
    """Raised when an incoming packet has a duplicate or stale sequence number."""

    def __init__(
        self,
        message: str,
        classification: str,
        sequence: int,
        latest_sequence: int | None = None,
    ):
        super().__init__(message)
        self.classification = classification
        self.sequence = sequence
        self.latest_sequence = latest_sequence


class StationDisconnectedError(StationReliabilityError):
    """Raised when a packet is sent to a disconnected station."""


class PendingQueueFullError(StationReliabilityError):
    """Raised when a station's delayed-packet queue is full."""


class PendingQueueEmptyError(StationReliabilityError):
    """Raised when a release is requested without a pending packet."""
