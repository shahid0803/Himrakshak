# Real-data intelligence

The independent [`himrakshak/data_intelligence/`](../../himrakshak/data_intelligence/)
package adds a versioned parameter registry, quality checks, provenance-aware
SQLite storage, source adapter boundaries, rainfall accumulation, inventory
filtering, and separate model eligibility pipelines. It does not alter the
existing station packet or illustrative risk engine.

## Commands

```powershell
python -m himrakshak.data_intelligence.pipeline --task registry
python -m himrakshak.data_intelligence.pipeline --task rainfall
python -m himrakshak.data_intelligence.pipeline --task terrain
python -m himrakshak.data_intelligence.pipeline --task landslides
python -m himrakshak.data_intelligence.pipeline --task all
```

The command is offline-safe and reports that no source data was downloaded.
Run source adapters from a dedicated worker with the required optional
dependencies and credentials. Earthdata credentials are read only from
environment variables; no credentials belong in the repository.

## API

- `GET /api/intelligence/registry` returns the parameter catalogue and
  approximate study extent.
- `GET /api/intelligence/observations` returns stored observations. Use
  `?parameter_id=...` to filter.
- `GET /api/intelligence/models` reports model readiness.

The default server database is in-memory to preserve the existing deployment
behavior. Set `HIMRAKSHAK_INTELLIGENCE_DB` to a SQLite path to persist
observations and model metadata. Large rasters stay on the filesystem.

## Data and model rules

Missing rainfall windows are `null`, never zero. Coordinates are validated
against WGS84 and the approximate research extent; this does not assert that a
point lies on NH-44. Open-Meteo and SoilGrids values are marked as model or
gridded estimates. Simulated Wokwi/Python readings are excluded from training.

Both models currently report
`NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS`, as documented in
[`MODEL_REPORT.md`](MODEL_REPORT.md). The existing rule-based engine remains
the backward-compatible behavior until genuine labels and independent
validation are available.
