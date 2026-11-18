# Wokwi remote-control firmware overlay

This overlay adds backend-controlled virtual sensor stimuli to the **new modular HimRakshak V2 Wokwi project**. It includes the 50 Hz scenario-mode vibration sampling fix and compact Serial Monitor logging.

## Apply to your existing Wokwi project

Open the existing HR-01 project: `https://wokwi.com/projects/480976780970275841`.

Replace the matching firmware files with the versions in this folder:

- `sketch.ino`
- `sensor_manager.h`
- `sensor_config.h`
- `simulation_scenarios.h`
- `station_config.h`

The `network_types.h`, `gts_root_r4.h`, and `tinyml_feature_contract.h` files are supplied as companion headers; add them if your project is missing them. The included `diagram.json` and `libraries.txt` are references for the existing circuit/library list and are not required to change when your current diagram is already working.

**Do not replace or delete `HimRakshakModel.h`.** The current live Wokwi project contains the Decision Tree header used in the successful NORMAL / WARNING / CRITICAL tests. Its embedded metadata hash is `a54175fdad7bb68f249e58fed707437aa6cf0f0c3d1ba5cd27941a8fd40db412`. That file is intentionally not included in this overlay.

Confirm `station_config.h` uses station `HR-01`, scenario stimulus mode is enabled, remote control is enabled, and `HIMRAKSHAK_LOG_LEVEL` is `1`. Save the project and start the simulation. Wait for Wi-Fi, NTP and backend sequence synchronization.

## How live control works

- The browser POSTs a versioned command to `/api/stations/HR-01/simulation-command`.
- ESP32 polls `/api/stations/HR-01/simulation-command?device_id=HR-01-WOKWI` every 3 seconds.
- The firmware applies custom values through `ScenarioManager` and the regular `SensorManager` pipeline, then acknowledges the version.
- Normal 10-second telemetry continues through the existing certificate-verified HTTPS `/readings` endpoint.
- The dashboard reads the latest accepted station reading and explicitly checks `transport=WOKWI_SOFTWARE_SIMULATION` before calling it Wokwi telemetry.

The 14 slider values are virtual stimulus controls. Derived quantities (rainfall accumulations, tilt rate and displacement rate) continue to be calculated by the firmware. The dashboard does not move Wokwi's graphical potentiometer positions.

## Serial output

`HIMRAKSHAK_LOG_LEVEL 1` is the default compact setting. It logs state/classification changes, command receipt/acknowledgement and concise telemetry/network events. Set it to `2` temporarily for detailed feature vectors and diagnostics. Keep it at `1` for the short Serial Monitor output used during the judge demo.

This is a Wokwi software demonstration. Sensor stimuli and model training labels are synthetic and are not field validated. Keep the existing 16-feature order, calibration, and model header unchanged.
