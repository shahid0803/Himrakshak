# TinyML training

The embedded model is a reproducible demonstration track, separate from the
research data-intelligence models.

## Dataset and features

`data/processed/himrakshak_synthetic_v2.csv` contains 240,000 generated rows:
80,000 each for `NORMAL`, `WARNING`, and `CRITICAL`. These rows are
class-conditioned synthetic samples generated from illustrative feature ranges
with measurement noise and seed `20261117`. The generator uses balanced
rejection sampling: a
candidate sample is retained only when the project's explicit demonstration
threshold rules assign the requested class. These are not collected
observations. The adjacent `.manifest.json` records the seed, exact class
counts, and CSV SHA-256 for reproducibility. The model uses these 16
features in fixed order:

1. rainfall intensity, 1-hour rainfall, 24-hour rainfall
2. surface moisture, subsurface moisture, pore pressure
3. tilt-X, tilt-Y, tilt rate
4. ground displacement, displacement rate
5. vibration RMS, peak, dominant frequency
6. soil temperature, groundwater depth

Battery, communication, and sensor-health state are deliberately excluded.

## Reproduce the export

```powershell
python -m pip install -r requirements-tinyml.txt
python train_tinyml.py --generate --count 240000 --seed 20261117
```

The command regenerates the 240,000-row CSV, `models/himrakshak_tinyml_v2.json`,
`models/training_metrics_v2.json`, and
`wokwi_esp32_demo/HimRakshakModel.h`.

The 240,000 records are split into 180,000 training rows and 60,000 stratified
holdout rows. Increasing this synthetic sample count adds generated combinations,
not real-world evidence or field validity. The selected algorithm is a compact
Decision Tree. Backend inference is
recomputed from validated features; device predictions are retained only for
comparison. Conservative fusion uses the more severe model/rule result, so
TinyML cannot lower an existing rule-based warning or critical state.

## Research track boundary

The susceptibility and dynamic early-warning pipelines report
`NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS`. Synthetic simulator and Wokwi
records are excluded. Field-grade training requires provenance-bearing,
time-aligned observations, event definitions, appropriate validation, and
geotechnical review.
