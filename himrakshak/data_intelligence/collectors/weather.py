"""Open-Meteo adapter using the optional requests package."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any
from urllib.parse import urlencode
from urllib.request import Request, urlopen
import json

OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast"


def fetch_open_meteo(
    latitude: float,
    longitude: float,
    *,
    start_date: str,
    end_date: str,
    timeout: float = 20.0,
) -> dict[str, Any]:
    query = urlencode({
        "latitude": latitude,
        "longitude": longitude,
        "start_date": start_date,
        "end_date": end_date,
        "hourly": "precipitation,rain,temperature_2m,snowfall,snow_depth,soil_moisture_0_to_7cm",
        "timezone": "UTC",
    })
    request = Request(f"{OPEN_METEO_URL}?{query}", headers={"User-Agent": "HimRakshak/18.1"})
    with urlopen(request, timeout=timeout) as response:
        if response.status != 200:
            raise RuntimeError(f"Open-Meteo returned HTTP {response.status}")
        return json.load(response)


def to_observations(payload: dict[str, Any], *, retrieved_at: str | None = None) -> list[dict[str, Any]]:
    hourly = payload.get("hourly", {})
    times = hourly.get("time", [])
    retrieved = retrieved_at or datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    # Open-Meteo reports hourly precipitation and rain as accumulated depth
    # for that interval (mm), not an instantaneous intensity (mm/hour).
    # Total precipitation includes liquid and solid precipitation; use the
    # dedicated `rain` series for rainfall-only accumulation.
    variables = {
        "rain": ("rainfall_accumulation_1h", "mm"),
        "precipitation": ("precipitation_accumulation_1h", "mm"),
        "temperature_2m": ("air_temperature", "deg C"),
        "snowfall": ("snowfall", "cm"),
        "snow_depth": ("snow_depth", "cm"),
        # SoilGrids/Open-Meteo volumetric water content is a fraction in m3/m3,
        # not a percentage. Keep the source unit; convert explicitly if needed.
        "soil_moisture_0_to_7cm": ("shallow_soil_moisture", "m3/m3"),
    }
    observations = []
    for index, timestamp in enumerate(times):
        for key, (parameter_id, unit) in variables.items():
            values = hourly.get(key, [])
            value = values[index] if index < len(values) else None
            observations.append({
                "observed_at": f"{timestamp}Z",
                "retrieved_at": retrieved,
                "parameter_id": parameter_id,
                "value": value,
                "unit": unit,
                "source": "Open-Meteo",
                "source_product": "forecast_or_historical_model",
                "is_simulated": False,
            })
    return observations
