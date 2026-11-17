"""Versioned parameter and study-area registry."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any, Callable

REGISTRY_VERSION = "18.1"


@dataclass(frozen=True)
class ParameterDefinition:
    parameter_id: str
    name: str
    description: str
    unit: str
    data_type: str
    source: str
    acquisition_frequency: str
    spatial_resolution: str | None
    validation: dict[str, Any]
    quality_status: str
    derived: bool
    static: bool

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


def _p(
    parameter_id: str,
    name: str,
    unit: str,
    source: str,
    *,
    description: str = "",
    frequency: str = "source dependent",
    resolution: str | None = None,
    validation: dict[str, Any] | None = None,
    quality: str = "unavailable_until_ingested",
    derived: bool = False,
    static: bool = False,
) -> ParameterDefinition:
    return ParameterDefinition(
        parameter_id,
        name,
        description or name,
        unit,
        "float",
        source,
        frequency,
        resolution,
        validation or {"finite": True},
        quality,
        derived,
        static,
    )


_definitions = [
    _p("rainfall_intensity_1h", "Rainfall intensity (1 hour)", "mm/hour", "GPM_IMERG/Open-Meteo", frequency="hourly", derived=True),
    _p("rainfall_accumulation_24h", "Cumulative rainfall (24 hours)", "mm", "GPM_IMERG/Open-Meteo", frequency="hourly", derived=True),
    _p("rainfall_accumulation_72h", "Cumulative rainfall (72 hours)", "mm", "GPM_IMERG/Open-Meteo", frequency="hourly", derived=True),
    _p("rainfall_antecedent_7d", "Antecedent rainfall (7 days)", "mm", "GPM_IMERG/Open-Meteo", frequency="daily", derived=True),
    _p("air_temperature", "Air temperature", "deg C", "Open-Meteo/IMD", frequency="hourly"),
    _p("snowfall", "Snowfall", "cm", "Open-Meteo", frequency="hourly"),
    _p("snow_cover_percentage", "Snow cover percentage", "%", "Sentinel-2", frequency="acquisition", derived=True),
    _p("snowmelt_estimate", "Snowmelt estimate", "mm water equivalent", "Open-Meteo", frequency="daily", derived=True),
    _p("pore_water_pressure", "Pore-water pressure", "kPa", "physical sensor", frequency="sensor interval"),
    _p("groundwater_depth", "Groundwater depth", "m", "CGWB", frequency="quarterly"),
    _p("shallow_soil_moisture", "Shallow soil moisture", "% VWC", "physical sensor/Open-Meteo", frequency="hourly"),
    _p("deep_soil_moisture", "Deep soil moisture", "% VWC", "physical sensor", frequency="sensor interval"),
    _p("stream_water_level", "Stream water level", "cm", "physical sensor", frequency="sensor interval"),
    _p("stream_level_rise_rate", "Stream-level rise rate", "cm/hour", "derived from sensor", frequency="hourly", derived=True),
    _p("ground_crack_width", "Ground crack width", "mm", "physical sensor", frequency="sensor interval"),
    _p("surface_displacement", "Surface displacement", "mm", "GNSS/InSAR", frequency="acquisition"),
    _p("displacement_velocity", "Displacement velocity", "mm/hour", "derived from displacement", frequency="hourly", derived=True),
    _p("displacement_acceleration", "Displacement acceleration", "mm/hour2", "derived from displacement", frequency="hourly", derived=True),
    _p("subsurface_displacement", "Subsurface displacement", "mm", "borehole sensor", frequency="sensor interval"),
    _p("ground_acceleration", "Ground acceleration", "g", "seismic sensor", frequency="event"),
    _p("ground_vibration", "Ground vibration characteristics", "feature vector", "seismic sensor", frequency="event"),
    _p("seismic_event", "Seismic event characteristics", "event", "seismic network", frequency="event"),
    _p("elevation", "Elevation", "m", "Copernicus DEM/OpenTopography", resolution="30 m", static=True),
    _p("slope_angle", "Slope angle", "degrees", "derived from DEM", resolution="30 m", derived=True, static=True),
    _p("slope_aspect", "Slope aspect", "degrees", "derived from DEM", resolution="30 m", derived=True, static=True),
    _p("terrain_curvature", "Terrain curvature", "1/m", "derived from DEM", resolution="30 m", derived=True, static=True),
    _p("soil_clay_content", "Soil clay content", "%", "ISRIC SoilGrids", resolution="250 m", static=True),
    _p("soil_sand_content", "Soil sand content", "%", "ISRIC SoilGrids", resolution="250 m", static=True),
    _p("soil_silt_content", "Soil silt content", "%", "derived from SoilGrids", resolution="250 m", derived=True, static=True),
    _p("soil_organic_carbon", "Soil organic carbon", "g/kg", "ISRIC SoilGrids", resolution="250 m", static=True),
    _p("geological_formation", "Geological formation", "category", "Geological Survey of India", static=True),
    _p("lithology", "Lithology", "category", "Geological Survey of India", static=True),
    _p("historical_landslide_density", "Historical landslide density", "events/km2", "NRSC inventory", derived=True, static=True),
    _p("distance_from_river", "Distance from river", "m", "GIS hydrography", derived=True, static=True),
    _p("distance_from_road_cuts", "Distance from highway and road cuts", "m", "verified NH-44 GIS", derived=True, static=True),
    _p("drainage_density", "Drainage density", "km/km2", "derived from DEM", derived=True, static=True),
    _p("ndvi", "Vegetation index", "ratio", "Sentinel-2", frequency="acquisition", derived=True),
    _p("vegetation_change", "Vegetation change", "ratio/change", "Sentinel-2", frequency="acquisition", derived=True),
]

PARAMETER_REGISTRY: dict[str, ParameterDefinition] = {item.parameter_id: item for item in _definitions}


@dataclass(frozen=True)
class StudyArea:
    name: str = "NH-44 Ramban-Ramsu-Banihal research extent"
    west: float = 74.85
    east: float = 75.65
    south: float = 32.95
    north: float = 33.65
    locations: tuple[str, ...] = ("Ramban", "Ramsu", "Banihal", "Khari", "Nachlana")
    verified_corridor: bool = False

    def contains(self, longitude: float, latitude: float) -> bool:
        return self.west <= longitude <= self.east and self.south <= latitude <= self.north

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


DEFAULT_STUDY_AREA = StudyArea()


def registry_as_dict() -> dict[str, Any]:
    return {
        "version": REGISTRY_VERSION,
        "parameters": [parameter.as_dict() for parameter in PARAMETER_REGISTRY.values()],
        "study_area": DEFAULT_STUDY_AREA.as_dict(),
    }
