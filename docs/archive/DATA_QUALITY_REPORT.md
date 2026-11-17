# Data quality report

- Invalid coordinates are rejected; valid points outside the approximate
  research extent are excluded.
- Corridor filtering requires an imported LineString and explicit buffer; the
  existing rectangle is not treated as an NH-44 geometry.
- Duplicate landslide records are retained but marked, rather than silently
  discarded.
- Missing rainfall values and missing time intervals remain missing.
- SoilGrids values are modelled gridded estimates, not direct geotechnical
  measurements.
- DEM derivatives require a projected CRS with metre units.
- Every persisted observation retains source, product, timestamps, unit,
  quality, resolution, processing version, and simulation status.
