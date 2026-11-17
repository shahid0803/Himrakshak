# HimRakshak model status

HimRakshak intentionally has two distinct model tracks. They must not be conflated.

## 1. Embedded TinyML demonstration model

**Status:** Trained and exported as a synthetic demonstration model.

- Model ID: `himrakshak-synthetic-demo-tree-v2`
- Type: compact DecisionTreeClassifier
- Features: 16 ordered environmental features in `himrakshak/tinyml.py`
- Dataset: 2,400 synthetic rows, evenly split across NORMAL/WARNING/CRITICAL scenario labels
- Export: `models/himrakshak_tinyml_v2.json`, `models/training_metrics_v2.json`, and `wokwi_esp32_demo/HimRakshakModel.h`
- Held-out synthetic-label agreement: 98.83% accuracy; 99.5% CRITICAL recall
- Field validated: **No**

The dataset is generated from illustrative virtual scenario profiles. It is suitable for demonstrating training, model export, and local inference parity. The metrics are not estimates of real-world landslide-prediction performance. The synthetic vibration-anomaly warning examples support the button-driven virtual demonstration, but do not establish that an isolated vibration event predicts a landslide.

Reproduce the model:

```powershell
python -m pip install -r requirements-tinyml.txt
python train_tinyml.py --generate
```

## 2. Research susceptibility and dynamic early-warning models

**Status:** `NOT_TRAINABLE_DUE_TO_INSUFFICIENT_LABELS`.

The research pipelines require genuine, provenance-bearing observations and:

- Susceptibility: spatially verified landslide inventory and appropriate background/non-event samples, plus terrain/geology/soil covariates.
- Dynamic early warning: time-aligned environmental observations and an explicitly defined future event horizon with positive and negative time windows.

Synthetic Wokwi/Python simulator records are excluded from research model eligibility. No real source data or event labels are bundled as if they were field observations.

## Interpretation

The TinyML demonstration proves that a compact model can be trained on a synthetic scenario dataset, exported to C++, and invoked by the ESP32 firmware contract. It does not prove that HimRakshak can predict actual landslides. Field-grade development requires calibrated instruments, independently labelled events, spatial/temporal validation, geotechnical review, and fail-safe deployment engineering.
