# Data acquisition report

| Source | Local path | Network path | Status |
|---|---|---|---|
| NRSC inventory | CSV/GeoJSON fixtures | Manual NRSC download | Implemented and tested locally |
| GPM IMERG V07 | Normalisation helper | Earthaccess discovery/download | Requires Earthdata credentials and optional `earthaccess` |
| IMD rainfall | CSV import | NetCDF through xarray | CSV tested; NetCDF dependency-gated |
| DEM | ESRI ASCII Grid | GeoTIFF through Rasterio | ASCII tested; GeoTIFF dependency-gated |
| SoilGrids | Cached response client | REST API | Implemented; live access not run |

Downloaded inputs belong under `data/raw/` and are ignored by Git. Secrets
must be supplied through environment variables and are never stored in the
database or repository.
