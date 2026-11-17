# TinyML training

The embedded model is a reproducible demonstration track, separate from the
research data-intelligence models.

## Dataset and features

`data/processed/himrakshak_synthetic_v2.csv` contains 2,400 generated rows:
800 each for `NORMAL`, `WARNING`, and `CRITICAL`. The model uses these 16
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
python train_tinyml.py --generate
```

The command regenerates the CSV, `models/himrakshak_tinyml_v2.json`,
`models/training_metrics_v2.json`, and
`wokwi_esp32_demo/HimRakshakModel.h`.

The selected algorithm is a compact Decision Tree. Backend inference is
recomputed from validated features; device predictions are retained only for
comparison. Conservative fusion uses the more severe model/rule result, so
TinyML cannot lower an existing rule-based warning or critical state.

## Research track boundary

The susceptibility and dynamic early-warning pipelines report
`NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS`. Synthetic simulator and Wokwi
records are excluded. Field-grade training requires provenance-bearing,
time-aligned observations, event definitions, appropriate validation, and
geotechnical review.
