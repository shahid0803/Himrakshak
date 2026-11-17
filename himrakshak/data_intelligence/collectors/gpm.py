"""NASA GPM IMERG retrieval boundary and precipitation normalisation."""

from __future__ import annotations

from datetime import datetime, timezone
import os
from pathlib import Path
from typing import Any, Iterable


def earthdata_credentials_configured() -> bool:
    return bool(os.environ.get("EARTHDATA_USERNAME") and os.environ.get("EARTHDATA_PASSWORD"))


def normalise_precipitation(
    records: Iterable[dict[str, Any]],
    *,
    source_product: str = "GPM_3IMERGHH_V07",
    retrieved_at: str | None = None,
) -> list[dict[str, Any]]:
    retrieved = retrieved_at or datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    result = []
    for record in records:
        timestamp = record.get("observed_at") or record.get("timestamp")
        if not timestamp:
            raise ValueError("GPM record requires an observed_at timestamp")
        value = record.get("value", record.get("precipitation_mm"))
        result.append({
            **record,
            "observed_at": str(timestamp),
            "value": None if value is None else float(value),
            "unit": "mm",
            "source": "NASA GPM",
            "source_product": source_product,
            "retrieved_at": retrieved,
            "is_simulated": False,
        })
    return result


def fetch_gpm(
    *,
    start: str,
    end: str,
    bounding_box: tuple[float, float, float, float],
    cache_dir: str | Path = "data/raw/gpm",
) -> list[Any]:
    """Discover IMERG granules through Earthaccess when installed."""
    try:
        import earthaccess  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "GPM retrieval requires optional earthaccess; install it in the collection worker"
        ) from exc
    if not earthdata_credentials_configured():
        raise RuntimeError("set EARTHDATA_USERNAME and EARTHDATA_PASSWORD for GPM retrieval")
    Path(cache_dir).mkdir(parents=True, exist_ok=True)
    earthaccess.login(strategy="environment")
    results = earthaccess.search_data(
        short_name="GPM_3IMERGHH",
        version="07",
        temporal=(start, end),
        bounding_box=bounding_box,
    )
    return list(earthaccess.download(results, local_path=str(cache_dir)))
