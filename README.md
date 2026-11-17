# HimRakshak V2

HimRakshak is a virtual ESP32 geotechnical-monitoring prototype. It combines
virtual sensor acquisition, local rule-based assessment, a compact TinyML
demonstration model, HTTPS telemetry, and a Python dashboard for three
independent virtual stations.

This repository demonstrates software and virtual hardware only. It does not
measure a real slope, implement physical LoRa, or provide a certified warning
system. It must not be used for evacuation, road closure, engineering safety,
or other public-safety decisions.

## Problem statement

Slope monitoring systems must combine heterogeneous measurements, continue
local operation when communication is unavailable, and expose enough telemetry
for an operator to understand the result. HimRakshak demonstrates those
control-flow and integration concerns while making the boundary between
simulated, derived, and externally sourced data explicit.

## Architecture

```text
virtual inputs -> ESP32 acquisition/filtering -> local rule/state machine
                                      -> LEDs and buzzer
                                      -> HTTPS telemetry -> Python API
                                                          -> validation,
                                                             inference,
                                                             station history,
                                                             dashboard
```

The backend is authoritative for accepted telemetry and recalculates risk. A
device-provided risk or model result is comparison metadata, not an authority.
See [the architecture document](docs/architecture.md) and the
[telemetry and API contracts](docs/telemetry-api.md).

## Technology stack

- ESP32 Arduino firmware, exercised in Wokwi.
- Python standard-library HTTP server and `unittest` suite.
- Vanilla HTML, CSS, and JavaScript dashboard.
- A compact scikit-learn Decision Tree exported to JSON and generated C++.
- Optional data-intelligence adapters for provenance-bearing research data.

## Eight environmental parameter groups

1. Rainfall intensity and seeded 1-hour/24-hour accumulation.
2. Surface and subsurface soil moisture.
3. Pore-water pressure.
4. Two-axis inclination and derived rate.
5. Ground displacement and derived rate.
6. Vibration RMS, peak, and frequency event template.
7. Soil temperature.
8. Groundwater depth.

The Wokwi circuit uses six potentiometers and a button. Tilt-Y,
displacement, vibration statistics, soil temperature, groundwater depth, and
rainfall history are derived or scripted proxies. Battery is separate device
health and is not a model feature. No calibrated field instrument is connected.
The [embedded and sensor documentation](docs/embedded-and-sensors.md) lists the
exact mapping.

## TinyML methodology and limitations

The embedded model uses 16 ordered features and a compact Decision Tree trained
on 2,400 synthetic scenario rows (800 per demonstration class). The selected
artifact is exported to JSON, metrics JSON, and C++. Its holdout metrics
describe agreement with synthetic labels only; they are not real-world
landslide-prediction performance or field validation. The separate research
susceptibility and dynamic early-warning tracks remain ineligible until
genuine, time-aligned observations and event labels are imported.

Read [TinyML training](docs/tinyml-training.md) and
[model evaluation](docs/model-evaluation.md) for provenance and interpretation.

## Installation and execution

The backend requires Python 3.10+ and no runtime package installation:

```powershell
python -m himrakshak.server
```

Open `http://127.0.0.1:8765/`. Optional model training dependencies
```

## Wokwi demonstration

Open [the Wokwi guide](docs/wokwi-guide.md) to upload the sketch, select a
station profile, inspect the serial output, and verify the virtual inputs.
Published Wokwi URLs are intentionally configured by the operator in
`wokwi-config.js`; no project IDs are fabricated.

## Testing

```powershell
python -B -m unittest discover -s tests -v
python -m compileall -q himrakshak tests scripts
```

The suite covers Python contracts, station behavior, data-intelligence
processing, frontend contracts, and firmware contracts. It does not replace a
live Arduino/Wokwi compile or external-service check. See
[testing and evidence](docs/testing.md).

## Known limitations

- All Wokwi inputs are virtual; several values are derived proxies.
- The TinyML dataset and labels are synthetic and not field validated.
- The risk thresholds are illustrative, not geotechnical limits.
- Backend state is bounded in memory and is lost on restart.
- HTTPS/Wi-Fi in Wokwi is not physical radio or LoRa validation.
- External data adapters require source files, credentials, optional packages,
  and independent quality review.
- Live Wokwi execution and published simulator URLs require operator setup.
- The prototype is not a life-safety or deployment-certified system.

## Documentation map

- [Architecture](docs/architecture.md)
- [Embedded and sensors](docs/embedded-and-sensors.md)
- [TinyML training](docs/tinyml-training.md)
- [Model evaluation](docs/model-evaluation.md)
- [Telemetry and API contracts](docs/telemetry-api.md)
- [Data sources and quality](docs/data-sources-and-quality.md)
- [Wokwi setup](docs/wokwi-guide.md)
- [Judge demonstration](docs/judge-demo.md)
- [Deployment](docs/deployment.md)
- [Testing and historical evidence](docs/testing.md)
