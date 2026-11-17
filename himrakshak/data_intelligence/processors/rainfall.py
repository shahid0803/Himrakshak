"""Rainfall accumulation with explicit gaps (never zero-filled)."""

from __future__ import annotations

from datetime import datetime, timedelta
from typing import Iterable, Mapping


def _time(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def accumulate_rainfall(
    observations: Iterable[Mapping[str, object]],
    *,
    timestamp_key: str = "observed_at",
    value_key: str = "value",
) -> list[dict[str, object]]:
    """Return rolling rainfall features for hourly observations.

    A window is ``None`` when any expected hourly interval is missing. This
    prevents an unavailable source from being interpreted as no rain.
    """
    rows = sorted(
        ({"timestamp": _time(str(row[timestamp_key])), "value": row.get(value_key)} for row in observations),
        key=lambda row: row["timestamp"],
    )
    result = []
    for current in rows:
        timestamp = current["timestamp"]
        item: dict[str, object] = {"observed_at": timestamp.isoformat(), "rainfall_1h": current["value"]}
        for hours, name in ((24, "rainfall_24h"), (72, "rainfall_72h"), (168, "rainfall_7d")):
            values = [row["value"] for row in rows if timestamp - timedelta(hours=hours - 1) <= row["timestamp"] <= timestamp]
            expected_start = timestamp - timedelta(hours=hours - 1)
            expected_timestamps = {
                expected_start + timedelta(hours=index) for index in range(hours)
            }
            timestamps = {
                row["timestamp"] for row in rows if expected_start <= row["timestamp"] <= timestamp
            }
            complete = timestamps == expected_timestamps and all(value is not None for value in values)
            item[name] = sum(float(value) for value in values) if complete else None
        result.append(item)
    return result
