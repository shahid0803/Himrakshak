"""ISRIC SoilGrids REST client with local caching and rate limiting."""

from __future__ import annotations

import json
from hashlib import sha256
from pathlib import Path
import time
from typing import Any
from urllib.parse import urlencode
from urllib.request import Request, urlopen

SOILGRIDS_URL = "https://rest.isric.org/soilgrids/v2.0/properties/query"


class SoilGridsClient:
    def __init__(self, cache_dir: str | Path = "data/raw/soilgrids", *, min_interval: float = 1.0, timeout: float = 30.0):
        self.cache_dir = Path(cache_dir)
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self.min_interval = min_interval
        self.timeout = timeout
        self._last_request = 0.0

    def fetch(self, longitude: float, latitude: float, *, properties: tuple[str, ...] = ("clay", "sand", "silt", "soc", "bdod", "phh2o"), depths: tuple[str, ...] = ("0-5cm",)) -> dict[str, Any]:
        query = [("lon", longitude), ("lat", latitude)]
        query.extend(("property", value) for value in properties)
        query.extend(("depth", value) for value in depths)
        key = sha256(urlencode(query).encode("utf-8")).hexdigest()
        cache_path = self.cache_dir / f"{key}.json"
        if cache_path.exists():
            return json.loads(cache_path.read_text(encoding="utf-8"))
        wait = self.min_interval - (time.monotonic() - self._last_request)
        if wait > 0:
            time.sleep(wait)
        request = Request(f"{SOILGRIDS_URL}?{urlencode(query)}", headers={"User-Agent": "HimRakshak/18.2"})
        with urlopen(request, timeout=self.timeout) as response:
            if response.status != 200:
                raise RuntimeError(f"SoilGrids returned HTTP {response.status}")
            payload = json.load(response)
        self._last_request = time.monotonic()
        cache_path.write_text(json.dumps(payload), encoding="utf-8")
        return payload


def fetch_soilgrids(longitude: float, latitude: float, **kwargs: Any) -> dict[str, Any]:
    return SoilGridsClient(**kwargs).fetch(longitude, latitude)
