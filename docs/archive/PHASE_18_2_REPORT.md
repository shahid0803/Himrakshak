# Phase 18.2 report

## Implemented

- NRSC-style CSV and GeoJSON inventory imports with event-date preservation,
  duplicate detection, coordinate validation, approximate-area filtering, and
  configurable LineString corridor buffers.
- Optional Shapefile import through `pyshp`; Point geometries are supported
  when that dependency is installed.
- IMD CSV ingestion with missing-value preservation and monthly climatology.
  NetCDF opens through optional xarray when installed.
- NASA GPM Earthaccess discovery for `GPM_3IMERGHH` V07, plus precipitation
  normalisation. Credentials are environment-only.
- ESRI ASCII Grid DEM ingestion and dependency-free elevation, slope, aspect,
  and curvature derivation. GeoTIFF metadata import uses optional Rasterio.
- SoilGrids REST client with local response caching and request throttling.
- SQLite ingestion-run and landslide provenance tables.
- Reproducible pipeline entry points for local rainfall, terrain, and
  landslide files.

## Tested versus credential/dependency gated

The CSV, GeoJSON, rainfall, ASCII DEM, corridor, provenance, and deduplication
paths are covered by local fixture tests. GPM Earthdata discovery, IMD NetCDF,
Shapefile, GeoTIFF derivatives, Whitebox flow accumulation, and SoilGrids live
HTTP access require optional packages or network/provider access and are not
claimed as completed live integrations in this environment.

The original API, firmware, simulator, TLS contract, and risk formula remain
unchanged.
