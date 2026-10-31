"""Independent virtual station state and deterministic simulation management."""

from __future__ import annotations

from collections import deque
from copy import deepcopy
from dataclasses import dataclass
from datetime import datetime, timezone
import threading
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
