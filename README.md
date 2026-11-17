# HimRakshak V2

**An ESP32-based edge-intelligence prototype for landslide and slope-condition monitoring.**

HimRakshak demonstrates how virtual environmental inputs, embedded processing, a compact TinyML model, local alerts, secure telemetry, a Python API, and a monitoring dashboard can be combined into one prototype.

> [!IMPORTANT]
> **Prototype and safety notice:** The checked-in hardware path is a Wokwi/virtual ESP32 demonstration. Its sensor values include simulated and derived proxies, and the TinyML training dataset contains synthetic scenario records. The project has not been validated using calibrated field instruments or real landslide event labels. It is not a certified warning system and must not be used to make evacuation, road-closure, engineering, or other public-safety decisions.

---

## Table of contents

1. [Project overview](#1-project-overview)
2. [Features](#2-features)
3. [System architecture](#3-system-architecture)
4. [Technology stack](#4-technology-stack)
5. [Repository structure](#5-repository-structure)
6. [Requirements](#6-requirements)
7. [Quick start: run the dashboard and API](#7-quick-start-run-the-dashboard-and-api)
8. [TinyML and machine-learning setup](#8-tinyml-and-machine-learning-setup)
9. [Dataset: use, inspect, and regenerate](#9-dataset-use-inspect-and-regenerate)
10. [Train and export the TinyML model](#10-train-and-export-the-tinyml-model)
11. [How TinyML is used in HimRakshak](#11-how-tinyml-is-used-in-himrakshak)
12. [Run and test the backend](#12-run-and-test-the-backend)
13. [Run the virtual ESP32 in Wokwi](#13-run-the-virtual-esp32-in-wokwi)
14. [Sensor and GPIO reference](#14-sensor-and-gpio-reference)
15. [Run the test suite](#15-run-the-test-suite)
16. [Optional data-intelligence tools](#16-optional-data-intelligence-tools)
17. [Deployment notes](#17-deployment-notes)
18. [Troubleshooting](#18-troubleshooting)
19. [Known limitations and future work](#19-known-limitations-and-future-work)
20. [Contributing and development workflow](#20-contributing-and-development-workflow)

---

## 1. Project overview

Landslide and slope-condition monitoring can involve several different kinds of observations, including rainfall, soil moisture, pore-water pressure, slope inclination, ground displacement, vibration, soil temperature, and groundwater depth. A monitoring prototype must also distinguish environmental conditions from device health and communication failures.

HimRakshak is an engineering demonstration of these software and embedded-system concepts. It uses a virtual ESP32 device to acquire and process input values, run local assessment logic, indicate a state using LEDs and a buzzer, and attempt HTTPS telemetry delivery. A Python service validates received packets, maintains the state of three demonstration stations, performs model-based assessment, and serves the dashboard.

The project has three main execution areas:

- **Embedded demonstration:** ESP32 Arduino firmware and a Wokwi circuit.
- **Software demonstration:** Python API, station simulation, risk assessment, and browser dashboard.
- **Machine-learning demonstration:** a Decision Tree trained using synthetic scenario data and exported to a compact C++ header for ESP32 inference.

The backend and the embedded device have separate responsibilities. Local ESP32 inference and local indicators do not require a successful cloud request. The backend provides telemetry ingestion, server-side validation and inference, station history, and a browser-facing API.

## 2. Features

- Virtual ESP32 firmware with sensor sampling and filtering.
- Local state handling for normal, warning, critical, sensor-fault, and recovery conditions.
- Local LED and buzzer indication.
- Compact Decision Tree TinyML model with 16 ordered input features.
- C++ model export for use by the ESP32 firmware.
- Python model runtime that validates the serialized model manifest and checks its feature contract.
- HTTPS telemetry path with a bounded in-memory packet queue and retry/backoff behavior in the firmware.
- Python API with schema validation, station identity, sequence handling, and server-side assessment.
- Three independent demonstration stations: `HR-01`, `HR-02`, and `HR-03`.
- Browser dashboard with connected-backend and standalone-simulation modes.
- Deterministic demonstration scenarios for normal, heavy rain, slope shift, and vibration burst.
- Unit and contract tests for Python, dashboard contracts, firmware contracts, and TinyML artifacts.
- Separate data-intelligence foundations for provenance-aware external observations.

## 3. System architecture

```text
                 VIRTUAL ENVIRONMENTAL INPUTS
            potentiometers, button, derived proxies
                              |
                              v
                    ESP32 ACQUISITION
                  sampling, filtering, checks
                              |
                              v
                 LOCAL EMBEDDED PROCESSING
                 +--------------------------+
                 | rule/state assessment   |
                 | TinyML Decision Tree    |
                 | health/validity checks  |
                 +--------------------------+
                     |                 |
                     v                 v
               LED / BUZZER       HTTPS TELEMETRY
                                       |
                              bounded retry queue
                                       |
                                       v
                                PYTHON API
                         schema validation and state
                         model inference and assessment
                                       |
                                       v
                              STATION MANAGEMENT
                         HR-01 / HR-02 / HR-03
                                       |
                                       v
                                WEB DASHBOARD
```

### Component responsibilities

| Component | Responsibility |
|---|---|
| ESP32 firmware | Reads virtual inputs, filters values, checks sensor health, runs local logic and TinyML inference, drives local indicators, and sends telemetry when the network is available. |
| TinyML C++ header | Contains the generated decision-tree arrays and inference function compiled into the ESP32 sketch. |
| Python TinyML runtime | Loads the JSON model artifact, verifies the feature order and SHA-256 manifest, and performs backend-side model inference. |
| Python API | Validates and normalizes readings, manages station state and sequence numbers, recalculates assessment, and exposes API endpoints. |
| Dashboard | Displays station status, readings, history, scenarios, and API connectivity. It can also run in clearly labelled standalone simulation mode. |
| Data-intelligence layer | Provides a separate foundation for importing and tracking external data with provenance and quality metadata. It does not silently convert simulated readings into field observations. |

### Important separation of responsibilities

- **TinyML** classifies a vector of environmental input features.
- **Rule-based safety logic** checks explicit prototype conditions and can independently maintain or raise an alert state.
- **Device health** (for example battery or sensor status) is not part of the 16-feature TinyML vector.
- **Communication status** is separate from environmental risk. A network failure does not mean the environment is safe or critical.
- **Cloud telemetry** is for reporting and dashboard visibility. It is not required for the ESP32 to execute its local model code.

## 4. Technology stack

| Layer | Technology |
|---|---|
| Embedded device | ESP32 DevKit / Arduino framework |
| Virtual hardware | Wokwi simulator |
| Embedded ML | Generated C++ Decision Tree inference |
| Model training | Python, pandas, NumPy, scikit-learn |
| Backend | Python standard-library HTTP server |
| Validation and tests | Python `unittest` |
| Dashboard | HTML, CSS, vanilla JavaScript |
| Model artifact | JSON metadata and exported C++ header |
| Data format | CSV for the synthetic training dataset; JSON for model and API data |

The standard API does not need pandas, NumPy, or scikit-learn to serve requests. Those packages are needed for the optional TinyML dataset/training/export workflow.

## 5. Repository structure

```text
HimRakshak/
├── README.md
├── requirements.txt
├── requirements-tinyml.txt
├── requirements-optional-ml.txt
├── requirements-optional-geospatial.txt
├── train_tinyml.py
├── index.html
├── wokwi-config.js
│
├── data/
│   ├── raw/
│   └── processed/
│       └── himrakshak_synthetic_v2.csv
│
├── models/
│   ├── himrakshak_tinyml_v2.json
│   └── training_metrics_v2.json
│
├── scripts/
│   └── generate_tinyml_dataset.py
│
├── himrakshak/
│   ├── server.py
│   ├── tinyml.py
│   ├── risk.py
│   ├── risk_v2.py
│   ├── schema.py
│   ├── stations.py
│   ├── simulator.py
│   ├── v2_simulator.py
│   └── data_intelligence/
│
├── wokwi_esp32_demo/
│   ├── sketch.ino
│   ├── diagram.json
│   ├── station_config.h
│   ├── HimRakshakModel.h
│   ├── gts_root_r4.h
│   └── sender_profiles/
│
├── site/
│   ├── index.html
│   └── wokwi-config.js
│
├── docs/
└── tests/
```

### Important files

| File | Purpose |
|---|---|
| `train_tinyml.py` | Reads the CSV dataset, trains/evaluates candidate Decision Trees, and exports model artifacts. |
| `scripts/generate_tinyml_dataset.py` | Creates the reproducible synthetic CSV dataset. |
| `data/processed/himrakshak_synthetic_v2.csv` | Bundled synthetic dataset used by the training script. |
| `models/himrakshak_tinyml_v2.json` | Serialized tree, feature contract, model metadata, metrics, and SHA-256 manifest. |
| `models/training_metrics_v2.json` | Training and holdout summary for the checked-in model. |
| `wokwi_esp32_demo/HimRakshakModel.h` | Generated C++ implementation included by the ESP32 firmware. |
| `himrakshak/tinyml.py` | Backend-side model loading, manifest validation, and inference. |
| `himrakshak/server.py` | Local API and dashboard server. |
| `wokwi_esp32_demo/sketch.ino` | ESP32 firmware sketch. |

## 6. Requirements

### Software requirements

- Python **3.10 or newer**.
- Git, if cloning the repository.
- A modern browser for the dashboard.
- An internet connection for cloning the repository and using the hosted Wokwi editor.
- Wokwi access for the virtual ESP32 demonstration.

### Hardware requirements

No physical sensor hardware is required for the repository's virtual demonstration. Wokwi simulates the ESP32 circuit and its input controls. A physical ESP32 and calibrated sensors would be required for a real hardware experiment, but physical deployment is outside the validated scope of this repository.

---

## 7. Quick start: run the dashboard and API

The Python backend uses the Python standard library. You do **not** need to install the TinyML training packages just to start the API and dashboard.

### 7.1 Clone the repository

**Bash (Linux, macOS, or Git Bash):**

```bash
git clone https://github.com/shahid0803/Himrakshak.git
cd Himrakshak
```

**PowerShell (Windows):**

```powershell
git clone https://github.com/shahid0803/Himrakshak.git
Set-Location Himrakshak
```

If you already have the repository locally, open a terminal in the folder that contains `README.md`, `train_tinyml.py`, and the `himrakshak/` directory.

### 7.2 Create and activate a virtual environment

A virtual environment keeps project packages isolated from other Python projects.

**Bash:**

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
```

**PowerShell:**

```powershell
py -3 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
```

If PowerShell blocks the activation script, use this command for the current terminal session, then activate again:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\.venv\Scripts\Activate.ps1
```

To leave the virtual environment later:

```bash
deactivate
```

### 7.3 Start the local dashboard and API

From the repository root, run:

```bash
python -m himrakshak.server --port 8765 --seed 42
```

Open this address in a browser:

```text
http://127.0.0.1:8765/
```

The server runs in the terminal. Keep that terminal open while using the dashboard. Press `Ctrl+C` to stop it.

### 7.4 Check the health endpoint

**Bash:**

```bash
curl -i http://127.0.0.1:8765/api/health
```

**PowerShell:**

```powershell
Invoke-RestMethod http://127.0.0.1:8765/api/health
```

The service should return a JSON health response. This verifies that the local Python service is responding; it does not verify Wokwi execution or real-world sensor operation.

---

## 8. TinyML and machine-learning setup

This section installs the packages required to regenerate the training dataset and train/export the Decision Tree model.

### 8.1 Required Python packages

The project declares the training dependencies in `requirements-tinyml.txt`:

| Package | Why it is used |
|---|---|
| `pandas` | Reads the training CSV and selects feature/target columns. |
| `numpy` | Supports tree export and numerical model operations. |
| `scikit-learn` | Trains and evaluates the Decision Tree Classifier. |

The exact declared minimum versions are:

```text
pandas>=2.0
numpy>=1.24
scikit-learn>=1.4
```

### 8.2 Install the ML libraries

Activate the virtual environment first, then run:

**Bash:**

```bash
python -m pip install --upgrade pip
python -m pip install -r requirements-tinyml.txt
```

**PowerShell:**

```powershell
python -m pip install --upgrade pip
python -m pip install -r requirements-tinyml.txt
```

You can confirm that the packages are available with:

```bash
python -c "import pandas, numpy, sklearn; print('pandas:', pandas.__version__); print('numpy:', numpy.__version__); print('scikit-learn:', sklearn.__version__)"
```

Do not install TensorFlow or TensorFlow Lite for this model. The checked-in TinyML path is a Decision Tree exported as C++ arrays and a small inference function; it is not a TensorFlow Lite model.

### 8.3 Runtime dependencies versus training dependencies

- `requirements.txt` is intentionally empty of third-party runtime dependencies because the API uses the Python standard library.
- `requirements-tinyml.txt` installs the packages for dataset reading and model training/export.
- `requirements-optional-ml.txt` is an optional research/experimentation list; it is not required for the main TinyML training script when `requirements-tinyml.txt` is installed.
- `requirements-optional-geospatial.txt` contains optional packages for geospatial data processing. It is not needed to run the dashboard, API, or core TinyML demonstration.

---

## 9. Dataset: use, inspect, and regenerate

### 9.1 Where is the dataset?

The repository already includes the training dataset here:

```text
data/processed/himrakshak_synthetic_v2.csv
```

You do not need to download a separate dataset to run the current TinyML demonstration.

The checked-in file contains **2,400 synthetic scenario rows**: 800 rows for each of the three demonstration classes. Its columns contain 16 model features plus the `risk_class` target column.

### 9.2 Dataset labels

| `risk_class` | Class name |
|---:|---|
| `0` | `NORMAL` |
| `1` | `WARNING` |
| `2` | `CRITICAL` |

These are generated scenario labels created by illustrative rules in `scripts/generate_tinyml_dataset.py`. They are not labels from observed landslide events or an authoritative geotechnical dataset.

### 9.3 The 16 model features

The order below is part of the model contract. Do not reorder, rename, or silently omit these columns when training a replacement model.

| # | CSV column | Meaning / unit used by the project |
|---:|---|---|
| 1 | `rainfall_intensity_mm_h` | Rainfall intensity, mm/h |
| 2 | `rainfall_1h_mm` | Accumulated rainfall over 1 hour, mm |
| 3 | `rainfall_24h_mm` | Accumulated rainfall over 24 hours, mm |
| 4 | `soil_moisture_surface_pct` | Surface soil moisture proxy, % |
| 5 | `soil_moisture_subsurface_pct` | Subsurface soil moisture proxy, % |
| 6 | `pore_water_pressure_kpa` | Pore-water pressure proxy, kPa |
| 7 | `tilt_x_deg` | X-axis tilt, degrees |
| 8 | `tilt_y_deg` | Y-axis tilt, degrees |
| 9 | `tilt_rate_deg_per_h` | Tilt rate, degrees/hour |
| 10 | `ground_displacement_mm` | Ground-displacement proxy, mm |
| 11 | `ground_displacement_rate_mm_per_h` | Displacement rate, mm/hour |
| 12 | `vibration_rms_g` | Vibration RMS proxy, g |
| 13 | `vibration_peak_g` | Vibration peak proxy, g |
| 14 | `vibration_dominant_hz` | Dominant vibration-frequency proxy, Hz |
| 15 | `soil_temperature_c` | Soil-temperature proxy, °C |
| 16 | `groundwater_depth_m` | Groundwater-depth proxy, m |
| Target | `risk_class` | Demonstration label: 0, 1, or 2 |

Battery percentage, Wi-Fi status, communication status, and device/sensor health are not part of this 16-feature model input vector.

### 9.4 Inspect the CSV from Python

```bash
python -c "import pandas as pd; d=pd.read_csv('data/processed/himrakshak_synthetic_v2.csv'); print(d.shape); print(d.head()); print(d['risk_class'].value_counts().sort_index())"
```

Expected shape for the bundled dataset: 2,400 rows and 17 columns (16 inputs plus one target).

### 9.5 Regenerate the dataset

The dataset generator uses Python's standard library and does not require pandas or scikit-learn. From the repository root, run:

```bash
python scripts/generate_tinyml_dataset.py --output data/processed/himrakshak_synthetic_v2.csv --count 2400 --seed 20261117
```

The script prints the output path, row count, class counts, and the source marker `synthetic_demo_only`.

To generate a different-size dataset, change `--count`. A count divisible by three gives equal numbers of rows for the three classes because the generator cycles through the three scenario labels. Keep the default count and seed if you want to reproduce the bundled demonstration dataset.

> **Dataset warning:** Regenerating the CSV does not make it real-world data. Do not describe generated rows as field measurements, historical landslides, or verified hazard observations.

---

## 10. Train and export the TinyML model

### 10.1 Train using the CSV already in the repository

After installing the ML dependencies, run this command from the project root:

```bash
python train_tinyml.py
```

If the CSV is missing, the script generates the default dataset before training. When the dataset exists, this command uses the existing CSV.

### 10.2 Regenerate the dataset and train in one command

To regenerate the default synthetic dataset and then train/export the model:

```bash
python train_tinyml.py --generate
```

The training script also accepts a row count:

```bash
python train_tinyml.py --generate --count 2400
```

### 10.3 What happens during training?

The script performs these operations:

1. Reads the CSV using pandas.
2. Checks that all 16 expected input columns and `risk_class` exist.
3. Rejects missing feature or target values.
4. Checks that the target contains all three classes: 0, 1, and 2.
5. Creates a stratified 75% training / 25% holdout split using a fixed split seed.
6. Trains Decision Tree candidates with maximum depths 4, 5, and 6.
7. Uses `min_samples_leaf=8`, balanced class weights, and a fixed model random seed.
8. Selects a candidate by prioritizing critical-class recall, then accuracy, then smaller tree size.
9. Writes model metadata and the tree structure to JSON.
10. Writes summary metrics to JSON.
11. Generates the C++ header used by the ESP32 sketch.

### 10.4 Files generated by training

| Output | Description |
|---|---|
| `models/himrakshak_tinyml_v2.json` | Serialized tree arrays, ordered feature contract, labels, metadata, evaluation results, limitations, and model hash. |
| `models/training_metrics_v2.json` | Training/holdout summary, metrics, class counts, feature list, and model hash. |
| `wokwi_esp32_demo/HimRakshakModel.h` | Generated C++ arrays and the `HimRakshakTinyML::predict()` function. |

The C++ header is generated from the tree. Do not hand-edit it; regenerate it using `train_tinyml.py` after a deliberate model change.

### 10.5 Verify the model artifacts

Run the TinyML-focused tests:

```bash
python -B -m unittest tests.test_tinyml_v2 -v
```

The tests check the model feature contract, artifact consistency, model hash, validation behavior, and expected model predictions for the demonstration scenarios.

### 10.6 Reproducibility note

The dataset generator uses a fixed default seed, and the training script uses a fixed split/model seed. The output metadata records the scikit-learn version and training timestamp. Different library versions can affect generated metadata or implementation details, so retain the JSON, metrics, and C++ header together as one model release.

---

## 11. How TinyML is used in HimRakshak

The model is trained on a computer and deployed as generated C++ logic. The ESP32 does not need to run Python, pandas, NumPy, or scikit-learn in order to execute the embedded decision tree.

### Model lifecycle

```text
Synthetic CSV
     |
     v
Python training (pandas + NumPy + scikit-learn)
     |
     v
Decision Tree candidate training and evaluation
     |
     v
JSON model + metrics JSON + generated C++ header
     |                                      |
     v                                      v
Python backend model runtime        ESP32 firmware compile
     |                                      |
     v                                      v
Server-side inference                Local embedded inference
```

### Output classes

| Numeric prediction | Output |
|---:|---|
| `0` | `NORMAL` |
| `1` | `WARNING` |
| `2` | `CRITICAL` |

### Where inference happens

- **On the ESP32:** `wokwi_esp32_demo/sketch.ino` includes `HimRakshakModel.h` and calls the generated tree inference function as part of the embedded processing path.
- **In the Python service:** `himrakshak/tinyml.py` loads `models/himrakshak_tinyml_v2.json`, checks the feature order and SHA-256 manifest, validates feature values, and evaluates the tree.
- **In the dashboard:** the dashboard displays server/device state and demonstration results; it is not itself the machine-learning training environment.

### Why use a Decision Tree?

A compact decision tree can be expressed as threshold comparisons and child-node indices. That structure is small and can be translated into straightforward C++ without embedding a Python runtime or a large inference framework on the ESP32.

### Model evaluation and interpretation

The currently checked-in model metadata reports a holdout accuracy of approximately **98.83%** and critical-class recall of approximately **99.50%** on a held-out portion of this synthetic dataset. These metrics measure agreement with the synthetic scenario labels only. They do **not** measure real landslide prediction accuracy, field reliability, sensor accuracy, or safety performance.

A future field model would require genuine, provenance-bearing, time-aligned observations, defensible event definitions, sensor calibration, geographically and temporally appropriate validation, independent test data, and review by qualified geotechnical specialists. Wokwi or Python simulator data must not be presented as field truth.

---

## 12. Run and test the backend

### 12.1 Start the server

```bash
python -m himrakshak.server --port 8765 --seed 42
```

The server binds to loopback by default. Visit `http://127.0.0.1:8765/`.

Other local port example:

```bash
python -m himrakshak.server --port 9000
```

### 12.2 Useful API endpoints

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/api/health` | Service health and model/service metadata. |
| `GET` | `/api/state` | Current aggregate state. |
| `GET` | `/api/stations` | List the demonstration stations. |
| `GET` | `/api/stations/HR-01` | Get one station's state. |
| `GET` | `/api/v2/features` | Inspect the ordered V2 model feature contract. |
| `GET` | `/api/v2/model` | Inspect model metadata and provenance. |
| `POST` | `/api/v2/demo` | Submit a V2 demonstration scenario. |
| `GET` | `/api/intelligence/registry` | Inspect the data-intelligence source registry. |
| `GET` | `/api/intelligence/observations` | Inspect imported data-intelligence observations. |

### 12.3 Inspect the model feature contract

**Bash:**

```bash
curl http://127.0.0.1:8765/api/v2/features
curl http://127.0.0.1:8765/api/v2/model
```

**PowerShell:**

```powershell
Invoke-RestMethod http://127.0.0.1:8765/api/v2/features
Invoke-RestMethod http://127.0.0.1:8765/api/v2/model
```

### 12.4 Submit a demonstration scenario

Supported V2 scenarios are `normal`, `heavy_rain`, `slope_shift`, and `vibration_burst`.

**Bash:**

```bash
curl -X POST http://127.0.0.1:8765/api/v2/demo \
  -H 'Content-Type: application/json' \
  -d '{"station_id":"HR-01","scenario":"slope_shift"}'
```

**PowerShell:**

```powershell
$body = @{ station_id = 'HR-01'; scenario = 'slope_shift' } | ConvertTo-Json
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8765/api/v2/demo `
  -ContentType 'application/json' `
  -Body $body
```

### 12.5 Expected demonstration states

| Scenario | Expected model class | Expected demonstration state |
|---|---|---|
| `normal` | `NORMAL` (0) | `NORMAL` |
| `heavy_rain` | `WARNING` (1) | `WARNING` |
| `slope_shift` | `CRITICAL` (2) | `CRITICAL` |
| `vibration_burst` | `WARNING` (1) | `WARNING` |

These are deterministic synthetic scenarios designed for software demonstration. They are not remotely controlled Wokwi input values and do not establish a real hazard threshold.

### 12.6 Legacy simulator CLI

The repository also contains a basic simulator for the legacy reading contract:

```bash
python -m himrakshak.simulator --scenario normal --count 3 --seed 42
python -m himrakshak.simulator --scenario heavy_rain --count 3 --seed 42
python -m himrakshak.simulator --scenario slope_shift --count 3 --seed 42
python -m himrakshak.simulator --scenario vibration_burst --count 3 --seed 42
```

The V2 dashboard scenarios use the V2 feature vector and `/api/v2/demo` endpoint described above.

---

## 13. Run the virtual ESP32 in Wokwi

The ESP32 circuit is intended to be run in Wokwi. The repository does not claim that the virtual circuit is a physical sensor installation.

### 13.1 Files to upload

Open the Wokwi ESP32 project/editor and use the files from `wokwi_esp32_demo/`:

- `sketch.ino`
- `diagram.json`
- `station_config.h`
- `gts_root_r4.h`
- `HimRakshakModel.h`

The sketch uses ESP32 Arduino core headers and APIs for Wi-Fi, HTTPS, timing, and standard C/C++ operations. The repository does not include a separate third-party Arduino `libraries.txt` for this sketch.

### 13.2 Select a station profile

The `sender_profiles/` directory contains profiles for the three virtual stations. Copy the desired profile over `station_config.h` before uploading/running the sketch.

**Bash:**

```bash
cp wokwi_esp32_demo/sender_profiles/hr01.h wokwi_esp32_demo/station_config.h
```

**PowerShell:**

```powershell
Copy-Item wokwi_esp32_demo\sender_profiles\hr01.h wokwi_esp32_demo\station_config.h -Force
```

Use `hr02.h` or `hr03.h` to configure the other station IDs. To restore the repository's default HR-01 profile, use the checked-in `station_config.h` from version control or copy the appropriate profile again.

### 13.3 Start and inspect the simulation

1. Open the Wokwi ESP32 project.
2. Upload or copy the five files listed above into the Wokwi project.
3. Confirm the `diagram.json` parts and connections load correctly.
4. Start the simulation.
5. Open the Serial Monitor at **115200 baud**.
6. Look for the TinyML initialization message and the model/inference logs.
7. Change the potentiometers and use the button to exercise the virtual input path.
8. Observe the LEDs and buzzer for the local state indication.

### 13.4 Demonstration sequence

1. Start from baseline inputs and observe the normal state.
2. Increase rainfall and moisture-related potentiometers to demonstrate changes in the processed input frame.
3. Increase the tilt proxy to exercise the warning/critical rule path.
4. Press the vibration button to trigger the discrete virtual event template.
5. Exercise an invalid ADC rail to inspect sensor diagnostics, where supported by the simulator.
6. Disconnect simulated Wi-Fi to observe local processing and pending telemetry behavior.
7. Restore communication and observe retry/recovery behavior.

### 13.5 Wokwi and cloud communication configuration

The firmware contains a configured HTTPS host and ingestion path. A Wokwi telemetry demonstration requires a reachable backend URL and a matching trusted TLS certificate embedded/configured for that host. If you deploy the backend to another host, review the firmware endpoint and certificate before expecting telemetry delivery.

Do not put administrative secrets or private credentials into the sketch or repository. A successful local Python test does not prove that the hosted Wokwi simulation or external endpoint is currently reachable.

---

## 14. Sensor and GPIO reference

The checked-in Wokwi circuit uses an ESP32 DevKit-style board, six potentiometers, a pushbutton, three LEDs, and a buzzer.

| Input/output | GPIO | Demonstration meaning |
|---|---:|---|
| Rainfall potentiometer | 33 | Rainfall intensity proxy |
| Surface moisture potentiometer | 34 | Surface moisture proxy |
| Tilt potentiometer | 35 | Primary/X-axis tilt proxy |
| Soil temperature potentiometer | 32 | Soil-temperature proxy |
| Subsurface moisture potentiometer | 36 | Subsurface moisture proxy |
| Pore-pressure potentiometer | 39 | Pore-pressure proxy |
| Vibration button | 18 | Discrete virtual vibration-event trigger |
| Green LED | 25 | Normal indication |
| Yellow LED | 26 | Warning indication |
| Red LED | 27 | Critical indication |
| Buzzer | 23 | Local audible alert pattern |

### Simulated and derived values

Several features are not direct measurements from physical instruments in this Wokwi circuit:

- Rainfall history uses a seeded/accelerated demonstration profile.
- Tilt-Y is a derived cross-axis proxy.
- Ground displacement and displacement rate are derived from a demonstration model and successive samples.
- Vibration RMS, peak, and dominant frequency are generated from a button-event template; they are not derived from a sampled accelerometer/FFT stream.
- Soil temperature is potentiometer-derived, not measured by a physical temperature probe.
- Groundwater depth is derived from demonstration values, not measured by a calibrated well transducer.
- The potentiometer-based moisture and pore-pressure inputs are proxies rather than calibrated field sensor readings.

Treat all such values as simulation or proxy values in reports and presentations. A real implementation would require appropriate sensor hardware, calibration, installation procedures, sampling design, data-quality checks, and field validation.

---

## 15. Run the test suite

Run all tests from the repository root.

```bash
python -B -m unittest discover -s tests -v
```

Run only the TinyML artifact and inference tests:

```bash
python -B -m unittest tests.test_tinyml_v2 -v
```

Run the firmware contract tests:

```bash
python -B -m unittest tests.test_firmware_contract -v
```

Compile Python modules to check syntax:

```bash
python -m compileall -q himrakshak tests scripts
```

The automated suite covers Python API/schema behavior, station independence, sequence handling, risk contracts, simulation, TinyML feature and artifact checks, frontend contracts, and firmware source/configuration contracts.

Automated Python tests do **not** replace these manual checks:

- compile and run the complete sketch in the hosted Wokwi environment;
- verify physical ESP32 board behavior;
- verify a real sensor installation or calibration;
- verify a current cloud deployment or remote service;
- conduct field validation of the model or prototype thresholds.

Test counts can change as the test suite changes. Run the commands above on the exact commit you are evaluating and report that run's output rather than copying a historical count into a release announcement.

---

## 16. Optional data-intelligence tools

The `himrakshak/data_intelligence/` package is separate from the synthetic TinyML demonstration. It provides a foundation for a source registry, quality/provenance metadata, and import/processing of selected local datasets.

### 16.1 Inspect the source registry

No external dataset download is required for the registry task:

```bash
python -m himrakshak.data_intelligence.pipeline --task registry
```

Run the offline-safe pipeline tasks:

```bash
python -m himrakshak.data_intelligence.pipeline --task all
```

### 16.2 Optional geospatial dependencies

Only install these packages if you are working with the optional geospatial data-processing adapters:

```bash
python -m pip install -r requirements-optional-geospatial.txt
```

That requirements file includes optional libraries such as pandas, GeoPandas, Shapely, Rasterio, and xarray. Some source providers require separate setup, credentials, downloads, or provider-specific packages. These dependencies are not needed for the core dashboard, API, or bundled TinyML model.

### 16.3 Importing external data

Use the documented `--input` argument with the matching task and file type. For example, a landslide inventory CSV can be imported into a local SQLite store:

```bash
python -m himrakshak.data_intelligence.pipeline \
  --task landslides \
  --input data/raw/your_landslide_inventory.csv \
  --db data/himrakshak-intelligence.sqlite
```

A rainfall CSV import additionally requires coordinates:

```bash
python -m himrakshak.data_intelligence.pipeline \
  --task rainfall \
  --input data/raw/your_rainfall_data.csv \
  --longitude 77.2 \
  --latitude 32.2 \
  --db data/himrakshak-intelligence.sqlite
```

A DEM import requires a projected CRS:

```bash
python -m himrakshak.data_intelligence.pipeline \
  --task terrain \
  --input data/raw/your_dem.asc \
  --crs EPSG:32643 \
  --db data/himrakshak-intelligence.sqlite
```

The file format and required columns must match the relevant importer. Check the data-source and quality documentation before using real inputs. Place downloaded source files under `data/raw/`; keep provider credentials in environment variables or a secure secret store, never in Git.

**Data eligibility rule:** Importing a file does not automatically make it valid machine-learning training data. Each observation needs source provenance, timestamps, units, coordinates where relevant, quality metadata, and a defensible target label/event definition. Simulated Wokwi and Python simulator records are not field observations.

---

## 17. Deployment notes

### Local development

Use the loopback server command:

```bash
python -m himrakshak.server --port 8765 --seed 42
```

The local server is intended for development and demonstration. State and station histories are held in memory and are reset when the process restarts unless a separately configured data-intelligence store is used.

### Static dashboard

The `site/` directory contains the static publishing entry point. A static page can demonstrate browser-side simulation, but static hosting does not run the Python API. It must not be described as connected-backend mode unless the browser is actually communicating with a running API.

### Remote server mode

The server supports an explicit remote-binding mode:

```bash
python -m himrakshak.server --allow-remote
```

In remote mode, the service binds to `0.0.0.0` and uses the platform `PORT` variable (default 10000 when not set). Configure the required admin key using the hosting provider's secret/environment-variable settings, not in source code. Remote mutation routes are disabled when no admin key is configured.

Relevant environment variables include:

| Variable | Purpose |
|---|---|
| `PORT` | Port assigned by the hosting platform. |
| `PYTHONUNBUFFERED` | Enables immediate process log output when set to `1`. |
| `HIMRAKSHAK_ADMIN_KEY` | Secret required for remote mutation routes. |
| `HIMRAKSHAK_INGEST_MIN_INTERVAL_SECONDS` | Optional per-station ingestion interval limit. |

Review the current host configuration and deployment logs before reporting a remote deployment as live. The repository configuration alone does not prove that a service is currently available.

---

## 18. Troubleshooting

### `python` is not recognized

Install Python 3.10 or newer, enable the option to add Python to PATH, open a new terminal, and check:

```powershell
python --version
```

On Windows, the Python launcher may work even when the `python` command does not:

```powershell
py -3 --version
```

### `No module named pandas` or `No module named sklearn`

Activate the virtual environment and install the ML requirements:

```bash
python -m pip install -r requirements-tinyml.txt
```

Check that the terminal is using the Python interpreter from `.venv`:

```bash
python -c "import sys; print(sys.executable)"
```

### Dataset file not found

Generate it from the repository root:

```bash
python scripts/generate_tinyml_dataset.py --output data/processed/himrakshak_synthetic_v2.csv --count 2400 --seed 20261117
```

Then train:

```bash
python train_tinyml.py
```

### Training says a feature or target column is missing

Do not rename or reorder the 16 feature columns. Compare the CSV header with the feature list in `scripts/generate_tinyml_dataset.py` and `himrakshak/tinyml.py`. The target column must be named `risk_class` and contain integer labels 0, 1, and 2.

### TinyML model hash or feature-order validation fails

Regenerate all model artifacts together using the current training script:

```bash
python train_tinyml.py
```

Do not manually edit only one of the JSON model, metrics JSON, or generated C++ header. Commit/release the matching artifacts together.

### Port 8765 is already in use

Choose a different port:

```bash
python -m himrakshak.server --port 9000
```

Then open `http://127.0.0.1:9000/`.

### Wokwi does not send telemetry

Check that the simulator has network access, the configured HTTPS endpoint is reachable, the host name matches the trusted certificate in the firmware, the backend is running, and the station ID matches a known station. Local TinyML inference and local LED/buzzer behavior can continue even if telemetry delivery fails.

### Wokwi compile error

Confirm that all required sketch files are present in the Wokwi project: `sketch.ino`, `diagram.json`, `station_config.h`, `gts_root_r4.h`, and `HimRakshakModel.h`. Check that the correct ESP32 board/framework is selected. The Python test suite does not compile the sketch using the hosted Wokwi or official Arduino toolchain.

---

## 19. Known limitations and future work

### Current limitations

- The sensor environment is virtual; this repository does not contain a calibrated physical field installation.
- The TinyML dataset is synthetic and labels are generated from demonstration rules.
- The model's holdout metrics measure synthetic-label agreement, not real landslide forecasting performance.
- Several Wokwi inputs are derived proxies, not direct measurements from the sensor type a field implementation might eventually use.
- The thresholds and risk logic are illustrative prototype settings, not validated geotechnical limits.
- The local device telemetry queue is bounded and held in RAM; it is not durable across a device reset.
- Backend station state is in memory and is reset when the process restarts.
- HTTPS/Wi-Fi simulation is not a physical radio test and does not implement or validate LoRa.
- The research data-intelligence workflows require genuine source observations, appropriate access, and data-quality review before any research model can be considered trainable.
- Hosted Wokwi execution and cloud deployment require separate operator verification.

### Future engineering work

1. Integrate calibrated sensors and document each sensor's electrical interface, range, accuracy, placement, and calibration procedure.
2. Collect time-aligned field observations with documented source provenance and expert-reviewed event labels.
3. Define geographic and temporal validation splits to reduce leakage between training and test observations.
4. Evaluate missing data, sensor drift, outliers, device faults, and uncertainty in a systematic way.
5. Assess the model against independent field data and compare it with transparent baseline/rule-based approaches.
6. Validate embedded timing, memory use, power consumption, and network behavior on the target physical ESP32.
7. Conduct independent engineering and safety review before considering any operational use.

---

## 20. Contributing and development workflow

1. Create a separate feature branch for your change.
2. Make the smallest scoped change possible.
3. Run the relevant focused tests.
4. Run the full test suite before opening a pull request.
5. For model changes, keep the dataset-generation procedure, feature order, JSON artifact, metrics, and C++ header consistent.
6. For sensor changes, update the pin/feature documentation and add tests for validity, units, provenance, and failure behavior.
7. Never commit API keys, private certificates, passwords, or private data.
8. Describe simulation, synthetic data, and field measurements accurately in code, documentation, screenshots, and presentations.

Suggested final validation commands:

```bash
python -B -m unittest discover -s tests -v
python -m compileall -q himrakshak tests scripts
git status
git diff --check
```

---

## Additional documentation

The `docs/` directory contains the detailed engineering notes for the individual subsystems. Start with the following documents when you need more detail than the setup guide above:

- `docs/architecture.md` — component boundaries and data flow.
- `docs/embedded-and-sensors.md` — virtual circuit, input mapping, and firmware behavior.
- `docs/tinyml-training.md` — dataset and model-generation workflow.
- `docs/model-evaluation.md` — interpretation of model metrics and validation boundaries.
- `docs/telemetry-api.md` — request fields, endpoints, and sequence behavior.
- `docs/data-sources-and-quality.md` — external source provenance and data-quality rules.
- `docs/wokwi-guide.md` — Wokwi setup and manual demonstration procedure.
- `docs/deployment.md` — local and remote server modes.
- `docs/testing.md` — automated and manual validation scope.

---

**HimRakshak V2 is an academic engineering prototype demonstrating embedded edge processing, TinyML model export, telemetry, and dashboard integration. Its synthetic model and virtual sensors are for demonstration and development, not public-safety decisions.**
