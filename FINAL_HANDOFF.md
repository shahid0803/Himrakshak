# HIMRAKSHAK — FINAL PROJECT HANDOVER (PHASE 11)

## 1. Project Overview

**HimRakshak** is an offline-first landslide early-warning demonstration system engineered for remote mountainous regions. It combines:
- A responsive, single-page web dashboard (`index.html`) featuring real-time risk visualization, slope illustration, sensor gauges, multi-station switching, scenario injection, and CSV session export.
- A zero-dependency Python backend utilizing only the standard library (`http.server`, `threading`, `json`, `dataclasses`, `hmac`).
- An explainable demonstration risk engine evaluating multi-parameter geotechnical thresholds (soil moisture, slope tilt, vibration level, and battery level).
- Multi-station management supporting three independently simulated virtual stations: `HR-01`, `HR-02`, and `HR-03`.
- Robust software-defined communication reliability controls (packet drop, delay queue, release, duplicate, stale sequence rejection, disconnect, and reconnect).
- Hardened dual-mode execution for local development (`127.0.0.1:8765`) and containerized cloud hosting (`0.0.0.0:$PORT`) with administrative protection and ingestion rate limiting.
- A virtual ESP32 hardware starter project designed for the Wokwi simulation environment.

---

## 2. Implemented Features

### Core Backend & Risk Engine
- **Explainable Demonstration Model (`himrakshak-demo-risk-engine-v1`):** Deterministic weighted scoring (moisture 35%, tilt 45%, vibration 20%) combined with critical single-sensor threshold triggers.
- **Contract Validation (`himrakshak.schema`):** Strict bounds checking on all numeric sensors (soil moisture: 0–100%, tilt: 0–15°, vibration: 0–100, battery: 0–100%). Supports legacy field aliases (`node`, `moisture`, `tilt`, `vibration`) and `SIMULATED` timestamp markers when transport is `WOKWI_SOFTWARE_SIMULATION`.
- **Three-Station Roster (`himrakshak.stations`):** Complete isolation between `HR-01` (Station 1), `HR-02` (Station 2), and `HR-03` (Station 3), each maintaining independent sequence counters, scenarios, latest packets, and bounded 100-event history.
- **Device Sequence Integrity:** External device sequences are preserved. Duplicate or out-of-order stale packets are rejected with HTTP 409 Conflict without altering accepted station state.

### Security & Deployment Readiness (Phase 11)
- **Dual-Mode Host & Port Binding:**
  - *Local Mode (Default):* Binds strictly to `127.0.0.1:8765`. Explicitly rejects `--host 0.0.0.0` unless `--allow-remote` is set.
  - *Cloud Mode (`--allow-remote`):* Binds to `0.0.0.0` and resolves dynamic `$PORT` injected by hosting environments (default: 10000).
- **Administrative Endpoint Protection:**
  - Mutation and fault-injection routes (`/simulate`, `/faults/*`) in cloud mode require the `X-HimRakshak-Admin` header verified via constant-time comparison (`hmac.compare_digest`) against `HIMRAKSHAK_ADMIN_KEY`.
  - If no admin key is configured in remote mode, mutation routes are disabled (HTTP 403) to prevent unauthorized tampering.
  - In local development mode without an admin key, all routes remain open for testing.
- **Thread-Safe Ingestion Rate Limiting:**
  - Enforces minimum interval (default 3.0 seconds in remote mode, configurable via `HIMRAKSHAK_INGEST_MIN_INTERVAL_SECONDS`) for registered stations, returning HTTP 429 Too Many Requests with `Retry-After`.
- **Same-Origin Architecture:**
  - Serves `index.html` directly from `/` and JSON API from `/api/` on the same origin. No wildcard CORS (`Access-Control-Allow-Origin: *`) required.
- **Socket Error Safety:**
  - Automatic request body draining (`_discard_request_body`) on error paths prevents socket connection aborts (`WinError 10053`) on Windows platforms.

---

## 3. Technology Stack

- **Backend:** Python 3.10+ (Standard Library only: `http.server`, `socketserver`, `argparse`, `dataclasses`, `hmac`, `json`, `math`, `time`, `threading`, `urllib.parse`, `pathlib`).
- **Frontend:** HTML5, CSS3 (CSS variables, flexbox/grid layout, responsive mobile design), Vanilla JavaScript (ES6+ async/await, Canvas API for real-time telemetry plotting). Zero external CDN or framework dependencies.
- **Testing:** Python `unittest` framework with standard library execution and high-level HTTP client smoke tests.
- **Virtual Hardware:** Arduino / C++ firmware (`wokwi_esp32_demo/sketch.ino`) configured for ESP32 DevKit v1 in Wokwi simulator.
- **Cloud Configuration:** Render Blueprint (`render.yaml`), GitHub Actions static deployment (`.github/workflows/pages.yml`).

---

## 4. Project Directory Structure

```text
HimRakshak/
├── .github/
│   └── workflows/
│       └── pages.yml                       # GitHub Pages deployment workflow (static dashboard)
├── .gitignore                              # Git exclusion patterns
├── FINAL_HANDOFF.md                        # This project handover document
├── README.md                               # Project presentation and general documentation
├── index.html                              # Official single-page dashboard (standalone & API-ready)
├── render.yaml                             # Render Web Service Blueprint (Phase 11)
├── requirements.txt                        # Standard-library-only declaration
│
├── docs/
│   ├── api-contract.md                     # Complete HTTP API specification & route contracts
│   ├── data-contract.md                    # Canonical sensor packet schema specification
│   ├── demo-acceptance-checklist.md        # Academic demonstration validation checklist
│   ├── demo-runbook.md                     # Step-by-step evaluation script
│   ├── deployment.md                       # Comprehensive local & Render deployment guide
│   ├── frontend-regression-checklist.md    # UI/UX visual and interaction regression guide
│   ├── multi-station.md                    # Multi-station isolation specification
│   ├── phase-4.3-regression-checklist.md   # Baseline regression checklist
│   ├── release-readiness-report.md         # Phase 8 historical evaluation report
│   ├── reliability-model.md                # Software communication failure simulation model
│   └── risk-model.md                       # Explainable geotechnical risk formula & rules
│
├── himrakshak/
│   ├── __init__.py                         # Package initialization
│   ├── reliability.py                      # Reliability statistics & FIFO delayed packet queue
│   ├── risk.py                             # Rule-based explainable risk calculation engine
│   ├── schema.py                           # Sensor data normalization & validation
│   ├── server.py                           # Dual-mode HTTP server, rate limiter & API router
│   ├── simulator.py                        # Deterministic seeded synthetic packet generator
│   └── stations.py                         # Three-station manager & sequence conflict handler
│
├── tests/
│   ├── __init__.py                         # Test package initialization
│   ├── test_demo_acceptance.py             # High-level end-to-end HTTP smoke test
│   ├── test_reliability.py                 # Communication fault simulation tests
│   ├── test_risk.py                        # Risk classification, boundary & weighting tests
│   ├── test_schema.py                      # Canonical contract, alias & validation tests
│   ├── test_server.py                      # Server configuration, routing, security & rate limit tests
│   ├── test_simulator.py                   # Simulator scenario, jitter & sequence tests
│   └── test_stations.py                    # Multi-station isolation & sequence integrity tests
│
└── wokwi_esp32_demo/
    ├── README.md                           # Wokwi simulation guide & control mapping
    ├── diagram.json                        # Wokwi circuit schematic (11 parts, 19 connections)
    └── sketch.ino                          # ESP32 Arduino firmware (sensors, LEDs, buzzer, Serial)
```

---

## 5. Local Installation and Run Instructions

### Prerequisites
- Python 3.10, 3.11, 3.12, 3.13, or 3.14.
- No third-party package installation (`pip install`) is required.

### Starting the Server in Local Mode (Default)

```powershell
# Open terminal in project root
python -m himrakshak.server
```

Output:
```text
HimRakshak server running on http://127.0.0.1:8765/
```

### Starting with a Custom Local Port

```powershell
python -m himrakshak.server --port 9000
```

### Starting in Cloud/Remote Mode (Explicit Opt-in)

```powershell
python -m himrakshak.server --allow-remote
```

Binds to `0.0.0.0` using `$PORT` (or default 10000).

---

## 6. Test Commands and Actual Results

All tests execute cleanly without network access or third-party dependencies:

```powershell
# Comprehensive unit and integration test suite
python -B -m unittest discover -s tests -v

# High-level end-to-end API acceptance flow
python -B -m unittest tests.test_demo_acceptance -v
```

### Actual Verified Test Results (Phase 11 Handover)

```text
Suite 1 (Comprehensive Tests):
Ran 111 tests in 12.463s
- test_demo_acceptance.py:     1 passed
- test_reliability.py:         5 passed
- test_risk.py:               10 passed
- test_schema.py:             51 passed (118 subtests passed)
- test_server.py:             26 passed
- test_simulator.py:          11 passed
- test_stations.py:            7 passed
Result: OK (111 passed, 0 failed, 0 errors, 118 subtests passed)

Suite 2 (Acceptance Flow):
Ran 1 test in 0.764s
Result: OK (1 passed, 0 failed, 0 errors)
```

---

## 7. How to Open and Use the Dashboard

### A. Connected Mode (With Local Python Server)
1. Run `python -m himrakshak.server`.
2. Open `http://127.0.0.1:8765/` in any modern web browser.
3. The indicator displays: **Connected backend mode · local Python API** (green badge).
4. Select stations (`HR-01`, `HR-02`, `HR-03`).
5. Trigger simulation scenarios (Normal, Heavy Rain, Slope Shift, Vibration Burst).
6. Test reliability controls (Drop, Delay, Release, Duplicate, Stale, Disconnect, Reconnect).
7. Export telemetry session as CSV via the **Export CSV** button.

### B. Standalone Mode (Browser-Only Offline)
1. Double-click `index.html` or open via `file:///.../index.html`.
2. The indicator displays: **Standalone browser mode · no API server**.
3. All risk evaluations and scenario controls execute entirely in client-side JavaScript.

---

## 8. Wokwi Simulator Status and Limitations

- **Current Implementation:** The virtual ESP32 project in `wokwi_esp32_demo/` simulates 2 potentiometers (moisture, tilt), 1 pushbutton (vibration burst), 3 status LEDs (Normal, Warning, Critical), and a piezo buzzer.
- **Output:** Outputs formatted canonical JSON packets to the Arduino `Serial` monitor at 115200 baud.
- **Current Integration Mode:** Manual copy/paste of JSON lines from Wokwi Serial Monitor into the dashboard's **Import virtual ESP32 packet** input field.
- **Limitations:**
  - The sketch currently lacks Wi-Fi (`WiFi.h`) and HTTPS (`HTTPClient.h`) network transmission code (scheduled for Phase 12).
  - Wokwi simulates software logic only; it does **not** simulate physical SX1262 LoRa radio transmissions or prove wireless radio propagation.

---

## 9. Cloud Deployment Readiness and Remaining Steps

### Completed Readiness Items (Phase 11)
- Configured dynamic `$PORT` and `0.0.0.0` binding.
- Implemented `render.yaml` Blueprint specification.
- Hardened public API with administrative authentication on mutation routes (`X-HimRakshak-Admin`).
- Implemented rate limiting on sensor reading ingestion.
- Preserved single-origin architecture eliminating CORS vulnerabilities.
- Added exact manual Render Blueprint deployment steps and a post-deployment endpoint checklist.
- Added clear browser feedback when protected remote administrative operations return HTTP 401 or 403.

### Manual deployment step
Apply the Blueprint from the Render dashboard:

1. Choose **New → Blueprint** and connect `shahid0803/Himrakshak` on the `main` branch.
2. Review the `himrakshak` service from the root `render.yaml` and keep the **Free** plan.
3. Allow Render to generate `HIMRAKSHAK_ADMIN_KEY`, then apply the Blueprint.
4. Wait for the deploy and health check to complete.
5. Use the generated HTTPS service URL to run the endpoint checklist in [`docs/deployment.md`](docs/deployment.md).

Do not record or invent a public URL in this repository; Render assigns it after deployment.

### Remaining Steps for Future Phases
- **Phase 12:** Implement `WiFi.h` and `HTTPClient.h` in `wokwi_esp32_demo/sketch.ino` targeting the cloud endpoint.
- **Phase 13:** Validate end-to-end live ingestion from Wokwi to cloud backend to live dashboard.
- **Phase 14:** Scale Wokwi virtual hardware across all three stations (`HR-01`, `HR-02`, `HR-03`).

---

## 10. Technical Honesty, Known Limitations & Boundary Claims

1. **No Physical LoRa Implemented:** The current system uses software-simulated sensor data and software-defined reliability queues. It does **not** communicate over physical LoRa frequencies (868/915/433 MHz) and does not contain physical SX1262 transceiver drivers.
2. **Demonstration Geotechnical Model:** Risk scoring and alert thresholds are illustrative software models designed for academic presentation. They are **not** scientifically calibrated or certified for real-world life-critical geological slope monitoring.
3. **Ephemeral Cloud Storage:** On free-tier cloud containers (Render), in-memory event history (100 events) and sequence counters reset when the container spins down after 15 minutes of inactivity.
4. **Cloud URL is environment-specific:** The Blueprint is deployment-ready, but the live service URL and runtime state exist only after the repository owner applies it in Render.
