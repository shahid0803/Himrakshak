# HimRakshak dataset and model readiness

## Embedded TinyML demonstration dataset

A separate synthetic demonstration dataset is included at:

`data/processed/himrakshak_synthetic_v2.csv`

It contains 2,400 generated examples (800 each for NORMAL, WARNING and CRITICAL) with 16 model features. It trains the compact embedded TinyML demo tree. Its labels describe synthetic scenario classes and must not be represented as observed landslide outcomes.

## Research susceptibility

The research susceptibility pipeline is not trainable from the current repository contents. A verified study-area corridor, genuine landslide inventory coverage, independent background samples, and provenance-bearing terrain/geology/soil features are still required.

## Research dynamic early warning

The research early-warning pipeline is not trainable from the current repository contents. It requires genuine timestamped environmental observations, an event inventory, an explicit forecast horizon, and carefully constructed positive and negative time windows. Missing rainfall intervals must remain missing, not be zero-filled.

## Current status distinction

- Embedded synthetic TinyML demonstrator: **TRAINED / EXPORTED / NOT FIELD VALIDATED**.
- Research susceptibility model: **NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS**.
- Research dynamic early-warning model: **NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS**.

These statuses are intentionally separate. The synthetic demonstration dataset must never make the research pipelines eligible.
