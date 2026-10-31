"""Deterministic communication-fault state for the HimRakshak demo."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


MAX_PENDING_PACKETS = 10


@dataclass
class ReliabilityStats:
    """Counters for one station's simulated communication outcomes."""

    packets_attempted: int = 0
    packets_delivered: int = 0
    packets_dropped: int = 0
    packets_delayed: int = 0
    duplicate_rejections: int = 0
    stale_rejections: int = 0
    disconnected_attempts: int = 0
    last_outcome: str | None = None
    last_delivered_sequence: int | None = None

    def as_dict(self, pending_count: int, connected: bool) -> dict[str, Any]:
        return {
            "connection_state": "connected" if connected else "disconnected",
            "packets_attempted": self.packets_attempted,
            "packets_delivered": self.packets_delivered,
            "packets_dropped": self.packets_dropped,
            "packets_delayed": self.packets_delayed,
            "pending_packets": pending_count,
            "duplicate_rejections": self.duplicate_rejections,
            "stale_rejections": self.stale_rejections,
            "disconnected_attempts": self.disconnected_attempts,
            "last_outcome": self.last_outcome,
            "last_delivered_sequence": self.last_delivered_sequence,
        }


@dataclass(frozen=True)
class PendingPacket:
    """A validated packet waiting for explicit deterministic release."""

    reading: dict[str, Any]
    queued_order: int
