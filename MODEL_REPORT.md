# Model report

## Status

Both model pipelines are implemented as separate eligibility and training
boundaries:

- **Susceptibility:** static terrain, geology, soil, vegetation, distance and
  drainage features with event/background labels.
- **Dynamic early warning:** timestamped environmental and sensor features with
  an explicitly labelled future event horizon.

No genuine labelled inventory or time-aligned event dataset is present in the
repository baseline. The current status of both models is therefore
`NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS`. The pipeline rejects simulated
Wokwi/Python simulator records and does not report fabricated metrics.

## Training contract

Use `himrakshak.data_intelligence.ml.train.train_model` after importing genuine
records. The implementation requires both target classes and minimum sample
counts, and reports missing optional ML dependencies instead of silently
falling back. Spatially separated validation for susceptibility and
chronological/event-group validation for early warning must be completed before
publishing metrics.

The existing rule-based demonstration risk engine remains the backward-
compatible production behavior until adequate labelled data and independent
evaluation are available.
