# HimRakshak — zero-budget software prototype

HimRakshak demonstrates how virtual slope-monitoring stations might produce
readings and warning states without cloud services or physical hardware. It is
for learning and hackathon prototyping, not field safety decisions or a
validated landslide-prediction system.

## Current Phase 6 foundation

Phases 1–3 preserve the improved standalone dashboard and add a
standard-library Python data contract, explainable risk engine, and
deterministic sensor simulator:

```text
HimRakshak/
├── index.html
├── README.md
├── requirements.txt
├── .gitignore
├── docs/
│   ├── data-contract.md
│   ├── api-contract.md
│   └── risk-model.md
├── himrakshak/
│   ├── __init__.py
│   ├── risk.py
│   ├── schema.py
│   ├── simulator.py
│   ├── server.py
│   └── stations.py
├── tests/
│   ├── test_risk.py
│   ├── test_schema.py
│   ├── test_server.py
│   ├── test_simulator.py
│   ├── test_stations.py
│   └── test_reliability.py
└── wokwi_esp32_demo/
    ├── README.md
    ├── diagram.json
    └── sketch.ino
```

The Python package validates packets, assesses the browser-compatible
demonstration rules, generates synthetic scenario packets, and serves a
local-only API for three independent virtual stations. There is no database or
live communication hardware.

## Open the dashboard

Open `index.html` directly in Chrome, Edge, or Firefox. It is self-contained,
has no runtime package installation, and preserves a browser-only simulation
mode. Use the scenario controls, simulated link toggle, JSON import, chart,
event history, and CSV export for demonstration.

## Public static demo

The hosted browser-only demonstration is available at
[`https://jaydeep2506747.github.io/HimRakshak/`](https://jaydeep2506747.github.io/HimRakshak/).
It runs the standalone simulation only. The Python API is not deployed to
GitHub Pages, so backend reliability operations require the local server
described below. Deployment details and troubleshooting are in
[`docs/deployment.md`](docs/deployment.md).

## Run the local API and dashboard

From the project root:

```powershell
python -m himrakshak.server --port 8765 --seed 42
```

Open `http://127.0.0.1:8765/`. The dashboard displays connected backend mode
when the local API is available, shows the fixed HR-01/HR-02/HR-03 roster, and
uses Python validation/risk results for the selected station.
Opening `index.html` directly still uses the standalone browser simulation
without a server or internet connection. API history is in memory only and is
lost when the server stops.

The endpoint details are documented in
[`docs/api-contract.md`](docs/api-contract.md), with station behavior in
[`docs/multi-station.md`](docs/multi-station.md) and deterministic
communication-fault behavior in
[`docs/reliability-model.md`](docs/reliability-model.md). Fault controls are
software-only and require the local API; they do not simulate physical radio
reliability.

For a first-time demonstration, follow
[`docs/demo-runbook.md`](docs/demo-runbook.md) and use
[`docs/demo-acceptance-checklist.md`](docs/demo-acceptance-checklist.md).
The repeatable high-level HTTP smoke test is:

```powershell
python -m unittest tests.test_demo_acceptance -v
```

## Run the tests

From this directory in Windows PowerShell:

```powershell
python -m unittest discover -s tests -v
```

If the Python launcher is used instead:

```powershell
py -m unittest discover -s tests -v
```

Phases 1–4 use only the Python standard library. `requirements.txt` documents
that no third-party package is currently required.

## Run the Python simulator

From the project root:

```powershell
python -m himrakshak.simulator --scenario normal
python -m himrakshak.simulator --scenario heavy_rain --count 3 --seed 42
```

Available scenarios are `normal`, `heavy_rain`, `slope_shift`, and
`vibration_burst`. Each compact JSON line separates canonical raw `reading`
data from an `assessment` containing exactly `risk_level`, `demo_score`,
`reasons`, `model_id`, and `illustrative`. The score and statuses are
illustrative only. The public simulator interface is
`SensorSimulator(scenario="normal", node_id="HR-01", seed=None)` with
`next_packet()`, `reset()`, and seeded one-decimal jitter around the documented
scenario baselines.

The formula, thresholds, output shape, and limitations are documented in
[`docs/risk-model.md`](docs/risk-model.md).

## Data contract

The canonical sensor fields, ranges, timestamp rules, and Wokwi exception are
documented in [`docs/data-contract.md`](docs/data-contract.md). The public
validator is `himrakshak.schema.validate_reading`.

The Wokwi files are in `wokwi_esp32_demo`. The ESP32 sketch emits compatible
field names, but its output is a software simulation and must be copied
manually from Serial Monitor into the dashboard. This project does not provide
real LoRa transmission or a live Wokwi-to-browser connection.

## Limitations

All sensor values and risk thresholds are illustrative. The project does not
establish physical sensor measurements, real radio range or packet delivery,
field reliability, outdoor validation, or scientifically validated landslide
prediction. Keep these limitations visible in demonstrations.
