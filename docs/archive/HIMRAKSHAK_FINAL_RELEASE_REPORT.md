# HimRakshak V2 – Final Local Release Report

**Verification date:** 2026-11-17  
**Release type:** Virtual embedded-system and TinyML demonstration prototype

## Release summary

The current package includes an expanded eight-group geotechnical parameter demonstration, a fixed 16-feature synthetic TinyML decision-tree model, V2 telemetry validation, backend scenario inference, dashboard presentation, and the existing autonomous ESP32 communication/alert architecture.

The system is intentionally described as a **virtual prototype**. The Wokwi circuit has six ADC1 potentiometers and a vibration-event pushbutton. Several model inputs are derived or scripted proxies, not separate physical sensor measurements:

| Parameter group | Current demonstration implementation |
|---|---|
| Rainfall | Potentiometer intensity; accelerated, seeded 1-hour/24-hour history |
| Multi-depth soil moisture | Independent surface and subsurface potentiometers |
| Pore-water pressure | Potentiometer proxy |
| Two-axis inclination | X-axis potentiometer; Y-axis derived cross-axis proxy; rate from successive samples |
| Ground displacement | Geometric tilt/lever-arm proxy; rate from successive samples |
| Vibration | Button-driven RMS/peak/frequency event template; not sampled accelerometer/FFT data |
| Soil temperature | Potentiometer-derived temperature proxy; not a DS18B20 reading |
| Groundwater depth | Formula-derived proxy from pore pressure and subsurface moisture; not a well sensor |

Battery is a separate fixed demonstration device-health estimate and is not a TinyML feature.

## TinyML model

- Model: `himrakshak-synthetic-demo-tree-v2`
- Algorithm: compact scikit-learn Decision Tree, exported to C++ in `wokwi_esp32_demo/HimRakshakModel.h`
- Dataset: 2,400 synthetic rows, 800 for each scenario class
- Features: 16 in a fixed versioned order
- Train/test split: 1,800 / 600
- Selected model: depth 4, 19 nodes
- Synthetic holdout accuracy: 98.83%
- Synthetic critical-class recall: 99.50%
- Confusion matrix (NORMAL, WARNING, CRITICAL rows/columns): `[[199, 1, 0], [1, 195, 4], [0, 1, 199]]`

These figures are agreement with synthetic demonstration labels generated from illustrative scenario profiles. They are **not** real-world landslide-prediction accuracy, field validation, or a safety certification. The research susceptibility and dynamic early-warning pipelines in `data_intelligence` remain ineligible until genuine, provenance-bearing, time-aligned observations and event labels are imported and independently evaluated.

## Architecture and compatibility

- V1 telemetry remains supported.
- V2 packets use `sensor_schema_version: 2`, 16 explicit environmental features, validity flags, model metadata and measurement provenance.
- Backend V2 inference is recomputed from validated features; device-provided prediction is retained for comparison, not trusted as the server result.
- Final risk fusion uses the more severe of the illustrative rule class and TinyML class. ML cannot lower an existing rule-based WARNING/CRITICAL state.
- Sensor health, battery/device health, environmental risk and communication status are separate.
- Existing certificate-verified HTTPS, bounded in-memory packet queue, retry/backoff, sequence synchronization and HTTP 409 reconciliation are retained.
- Three station identities remain available: HR-01, HR-02 and HR-03.

## Data-intelligence layer

The research-data package retains the 38+ parameter catalogue, source metadata, quality checks, SQLite provenance storage, rainfall-gap handling, DEM terrain processing, and import adapters. Source adapters require real downloaded source files, configured credentials, or their specified optional dependencies. No live external source dataset is claimed as included in this ZIP. The pipeline reports `NO_SOURCE_DATA_IMPORTED` when no source observations were actually ingested.

Unit handling was tightened for Open-Meteo: rainfall-only hourly accumulation is mapped from `rain`, total precipitation is kept separately, snow depth is not mapped to snow-cover percentage, and volumetric soil moisture is retained in `m3/m3`.

## Local validation performed for this package

- Complete Python unittest suite: **152 tests passed**.
- TinyML dataset regeneration and training completed.
- 2,400 rows regenerated with 800 examples per class.
- Model JSON, metrics JSON and generated C++ header regenerated.
- Native C++11 parity check of the exported model header passed for all four judge scenarios: `normal` -> 0, `heavy_rain` -> 1, `slope_shift` -> 2, and `vibration_burst` -> 1.
- Wokwi `diagram.json` parsed successfully.
- Backend/API smoke checks and dashboard response were checked locally.
- Root and GitHub Pages dashboard entry points were checked for synchronisation.

These checks do not constitute a live Arduino ESP32 build or Wokwi runtime session. The full `.ino` sketch still requires manual compilation and execution in Wokwi before presenting it as a working virtual circuit. Published Wokwi simulator URLs also remain intentionally blank until the team publishes its HR-01/HR-02/HR-03 projects and enters their real official URLs in `wokwi-config.js`.

## Remaining limitations before a judge demonstration

1. Compile and run the ESP32 sketch in Wokwi; this package could not run the hosted simulator or Arduino ESP32 toolchain in its packaging environment.
2. Confirm all six potentiometers, the pushbutton, three LEDs and buzzer respond as documented.
3. Verify Wi-Fi/HTTPS delivery in the actual Wokwi session; local backend tests do not prove cloud delivery.
4. Publish Wokwi station projects and configure their real URLs if the website iframe is to show the live circuit.
5. Explain clearly which inputs are potentiometer/button proxies and which values are derived.
6. Present TinyML metrics only as synthetic-label agreement; do not call the model a validated landslide predictor.
7. Do not use this academic prototype for public-safety decisions or real evacuation/closure actions.

## Run locally

```powershell
python -m pip install -r requirements-tinyml.txt
python -m himrakshak.server
```

Open `http://127.0.0.1:8765/`.

To retrain and export the checked-in synthetic demo model:

```powershell
python train_tinyml.py --generate
```

To run regression tests:

```powershell
python -B -m unittest discover -s tests -v
python -m compileall -q himrakshak tests scripts
```
