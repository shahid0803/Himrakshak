# Real data sources

The intelligence layer does not fabricate observations. Each record retains
its source, product, timestamps, quality status, coordinates, and whether it
is simulated. The initial study extent is an approximate rectangle
(74.85–75.65 E, 32.95–33.65 N), not a verified NH-44 corridor polygon.

| Source | Official link | Format/resolution | Access and limitations |
|---|---|---|---|
| NASA GPM IMERG V07 | https://gpm.nasa.gov/data/directory | Half-hourly precipitation granules | Earthdata account and optional `earthaccess`/`gpm-api`; missing granules remain missing |
| IMD gridded rainfall | https://www.imdpune.gov.in/cmpg/Griddata/Rainfall_25_NetCDF.html | NetCDF gridded rainfall | Manual/download availability varies; optional xarray/netCDF support |
| Open-Meteo | https://open-meteo.com/ | Hourly model weather | Model estimates, not physical station observations; public API limits apply |
| Sentinel-1 | https://dataspace.copernicus.eu/ | GRD/SLC acquisitions | Copernicus authentication; imagery is not displacement without a validated InSAR workflow |
| Sentinel-2 | https://dataspace.copernicus.eu/ | Surface reflectance imagery | Cloud masking and valid-pixel checks are required before NDVI |
| Copernicus DEM/OpenTopography | https://opentopography.org/ | DEM, typically 30 m | Terrain derivatives require projected CRS and optional Rasterio/Whitebox workers |
| ISRIC SoilGrids | https://rest.isric.org/soilgrids/v2.0/docs | 250 m predicted soil properties | Predictions, not direct geotechnical measurements; cache and respect limits |
| Geological Survey of India | https://bhukosh.gsi.gov.in/Bhukosh/MapViewer.aspx | GIS layers, scale varies | Manual import is supported; restricted layers are not scraped |
| NRSC landslide inventory | https://www.nrsc.gov.in/nrscnew/resources_atlas_landslide.php | CSV/Shapefile/GeoJSON where available | Event labels are retained; inventory completeness and dates vary |
| CGWB/NWDP | https://cgwb.gov.in/ and https://www.nwdp.nwic.gov.in/ | Monitoring observations, commonly quarterly | Manual import; not real-time telemetry and no station proximity is assumed |

Install `requirements-optional-geospatial.txt` only in a dedicated collection
worker environment. API runtime dependencies remain unchanged.
