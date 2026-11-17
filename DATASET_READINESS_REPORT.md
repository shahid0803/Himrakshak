# Dataset readiness report

## Susceptibility

The inventory and terrain/soil ingestion paths now produce provenance-bearing
records suitable for a later spatial join. A verified NH-44 centreline and
genuine inventory coverage are still required before generating independent
positive/background evaluation groups.

## Early warning

Rainfall records can be accumulated without zero-filling gaps. A genuine
timestamped event inventory and matching environmental observations are still
required. No artificial non-event windows or labels are generated.

## Current model status

Both pipelines remain:

`NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS`

This is an evidence-based status, not a failed-data fallback.
