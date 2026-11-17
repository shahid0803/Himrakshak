# Phase 18 baseline audit

## Scope

The repository is a Python standard-library HTTP server with a vanilla
JavaScript dashboard and Wokwi/ESP32 demonstration firmware. It is not a
production geospatial or machine-learning system.

## Existing contracts

- Canonical telemetry is validated by `himrakshak.schema.validate_reading`.
- Legacy `node`, `moisture`, `tilt`, and `vibration` aliases remain accepted.
- The risk engine in `himrakshak.risk` is an illustrative, rule-based model.
- `HR-01`, `HR-02`, and `HR-03` are fixed in-memory station identities.
- `/api/readings`, `/api/simulate`, `/api/state`, `/api/stations`, and
  `/api/health` are existing routes.
- The Wokwi sketch uses HTTPS certificate verification and the existing packet
  contract. No TLS or firmware changes are required for data intelligence.
- There is no persistent database, external data collector, or ML dependency.
- Existing tests use `unittest`; the baseline suite contains 126 passing tests.

## Environment

The checked-in virtual environment runs Python 3.14.6. `requests`, pandas,
GeoPandas, Shapely, Rasterio, xarray, scikit-learn, XGBoost, SHAP, IMDLIB,
Earthaccess, GPM-API, and PySTAC-Client are not installed. The new layer
therefore has a dependency-free core and optional adapters which fail with
actionable messages rather than silently substituting data.

## Preservation decisions

The new `himrakshak.data_intelligence` package is independent of the existing
station and risk modules. Simulated Wokwi readings are marked and rejected by
model dataset builders. External sources retain provenance and missing data is
never converted to zero. Heavy geospatial processing remains outside the API
runtime.
