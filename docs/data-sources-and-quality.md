# Data sources and quality

The data-intelligence package is a separate research-data foundation. It
retains source, product, timestamps, units, coordinates, quality, resolution,
processing version, and simulation status for persisted observations.

## Source status

| Source | Status and limitation |
|---|---|
| NRSC inventory | CSV/GeoJSON local import; manual official downloads |
| NASA GPM IMERG V07 | Earthdata credentials and optional `earthaccess` |
| IMD rainfall | CSV path tested; NetCDF needs optional xarray |
| DEM | ESRI ASCII path tested; GeoTIFF needs Rasterio |
| SoilGrids | Cached REST client; modelled estimates, not direct measurements |
| Open-Meteo | Hourly model estimates, not physical station observations |
| Sentinel-1/2, Copernicus DEM, GSI, CGWB/NWDP | Manual/source-specific access |

Official source references are retained in the source adapters and prior
historical report in [archive](archive/README.md). Downloaded inputs belong
under `data/raw/`; credentials are environment-only.

## Quality and eligibility rules

- Invalid coordinates are rejected; the approximate extent is not an NH-44
  corridor polygon.
- Corridor filtering requires an imported LineString and explicit buffer.
- Duplicate inventory records are marked, not silently discarded.
- Missing rainfall values and intervals remain missing, never zero-filled.
- DEM derivatives require a projected CRS with metre units.
- Simulated Wokwi/Python readings cannot qualify as research training data.
- The current research susceptibility and early-warning models are not
  trainable without genuine labels and time alignment.

Run the offline-safe foundation tasks with:

```powershell
python -m himrakshak.data_intelligence.pipeline --task registry
python -m himrakshak.data_intelligence.pipeline --task all
```

Optional adapters should run in a dedicated worker with their documented
dependencies and provider access. The API reports `NO_SOURCE_DATA_IMPORTED`
when no real observations have been ingested.
