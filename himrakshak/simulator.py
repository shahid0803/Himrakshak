"""Deterministic standard-library sensor simulator for HimRakshak."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import random
from typing import Any

from .risk import assess_risk
from .schema import validate_reading


SCENARIOS = {
    "normal": {
        "soil_moisture_pct": 32.0,
        "tilt_deg": 1.2,
        "vibration_level": 12.0,
        "battery_pct": 82.0,
    },
    "heavy_rain": {
        "soil_moisture_pct": 78.0,
        "tilt_deg": 4.5,
        "vibration_level": 30.0,
        "battery_pct": 80.0,
    },
    "slope_shift": {
        "soil_moisture_pct": 70.0,
        "tilt_deg": 9.0,
        "vibration_level": 55.0,
        "battery_pct": 78.0,
    },
    "vibration_burst": {
        "soil_moisture_pct": 40.0,
        "tilt_deg": 1.5,
        "vibration_level": 88.0,
        "battery_pct": 85.0,
    },
}

JITTER_BOUNDS = {
    "soil_moisture_pct": (-1.5, 1.5),
    "tilt_deg": (-0.3, 0.3),
    "vibration_level": (-2.0, 2.0),
    "battery_pct": (-1.0, 1.0),
}


class SensorSimulator:
    """Generate seeded, jittered synthetic packets for one virtual node."""

    def __init__(
        self,
        scenario: str = "normal",
        node_id: str = "HR-01",
        seed: int | None = None,
    ):
        if scenario not in SCENARIOS:
            choices = ", ".join(sorted(SCENARIOS))
            raise ValueError(f"unknown scenario {scenario!r}; choose from {choices}")
        if not isinstance(node_id, str) or not node_id.strip():
            raise ValueError("node_id must be a non-empty string")
        self.node_id = node_id.strip()
        if seed is not None and (isinstance(seed, bool) or not isinstance(seed, int)):
            raise ValueError("seed must be an integer when supplied")
        self.scenario = scenario
        self.seed = seed
        self._random = random.Random(seed)
        self._sequence = 0

    @property
    def sequence(self) -> int:
        """Return the next sequence number that will be generated."""

        return self._sequence

    def reset(self) -> None:
        """Reset sequence numbering and restore the original random seed."""

        self._sequence = 0
        self._random = random.Random(self.seed)

    def next_packet(self) -> dict[str, Any]:
        """Generate and validate the next packet for the configured scenario."""

        baseline = SCENARIOS[self.scenario]
        values = {
            field: round(
                baseline[field] + self._random.uniform(*JITTER_BOUNDS[field]), 1
            )
            for field in JITTER_BOUNDS
        }
        packet = {
            "node_id": self.node_id,
            "sequence": self._sequence,
            "timestamp": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            **values,
            "transport": "PYTHON_SIMULATOR",
        }
        self._sequence += 1
        return validate_reading(packet)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate HimRakshak synthetic telemetry and demo assessments."
    )
    parser.add_argument(
        "--scenario",
        choices=sorted(SCENARIOS),
        default="normal",
        help="scenario to generate (default: normal)",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=1,
        help="number of packets to print (default: 1)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="optional reproducibility seed; fixed scenarios are deterministic",
    )
    parser.add_argument("--node-id", default="HR-01", help="virtual node identifier")
    return parser


def main(argv: list[str] | None = None) -> int:
    """Run the simulator CLI and print one JSON result per packet."""

    parser = _build_parser()
    args = parser.parse_args(argv)
    if args.count < 1:
        parser.error("--count must be at least 1")

    try:
        simulator = SensorSimulator(
            scenario=args.scenario, node_id=args.node_id, seed=args.seed
        )
    except ValueError as exc:
        parser.error(str(exc))
    for _ in range(args.count):
        reading = simulator.next_packet()
        print(json.dumps({"reading": reading, "assessment": assess_risk(reading)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
