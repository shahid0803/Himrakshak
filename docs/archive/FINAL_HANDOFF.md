# HimRakshak V2 – Final Project Handover

> Current V2 implementation and validation are documented in
> [`HIMRAKSHAK_FINAL_RELEASE_REPORT.md`](HIMRAKSHAK_FINAL_RELEASE_REPORT.md).
> This file supersedes the earlier four-input project summary below it.

## Current project summary

HimRakshak V2 is a virtual ESP32-based, multi-parameter geotechnical monitoring and TinyML demonstration. It presents eight environmental parameter groups as 16 ordered numerical model features, applies a compact synthetic Decision Tree on the embedded path, compares it with an illustrative rule-based assessment, and uses conservative fusion so the model cannot lower a rule-based WARNING or CRITICAL state.

The project remains aligned to Theme 3 through autonomous embedded systems, edge inference and fault-tolerant device architecture. It does not implement quantum computing or semiconductor fabrication.

## Current environmental feature groups

1. Rainfall intensity and accelerated/seeded 1-hour and 24-hour accumulation.
2. Surface and subsurface soil moisture.
3. Pore-water pressure.
4. X/Y tilt and derived tilt rate.
5. Ground displacement and derived rate.
6. Vibration RMS/peak/frequency event templates.
7. Soil temperature.
8. Groundwater depth.

### Important virtual hardware disclosure

The Wokwi circuit uses six ADC1 potentiometers and a pushbutton. Some features are derived or synthetic proxies: seeded rainfall history, tilt-Y, displacement, displacement rate, vibration metrics, soil temperature and groundwater depth. It does not contain a calibrated rain gauge, dual-axis IMU, accelerometer waveform sampler, piezometer, extensometer, DS18B20 or groundwater well instrument. Battery is a separate fixed demo-health estimate.

## TinyML status

The included 16-feature Decision Tree was trained using 2,400 synthetic scenario rows and exported to JSON/metrics/C++. The selected test split contains 600 synthetic rows. Current synthetic-label agreement is 98.83% accuracy and 99.5% CRITICAL recall. This is not field validation or real-world landslide prediction accuracy.

The separate research susceptibility and dynamic early-warning model pipelines remain not trainable until genuine field/source observations and event labels are ingested.

## Run locally

```powershell
python -m pip install -r requirements-tinyml.txt
python -m himrakshak.server
```

Open `http://127.0.0.1:8765/`.

Run the V2 demonstration from the **Multi-Parameter Geotechnical Intelligence** panel. Select HR-01, HR-02 or HR-03 and run Normal, Heavy rain, Slope shift or Vibration burst.

## Retrain the synthetic TinyML model

```powershell
python train_tinyml.py --generate
```

This regenerates the CSV, JSON model, metrics and embedded C++ header. Do not describe the synthetic data as field data.

## Test

```powershell
python -B -m unittest discover -s tests -v
python -m compileall -q himrakshak tests scripts
```

## Wokwi judge demonstration

1. Open `https://wokwi.com/esp32`.
2. Upload `wokwi_esp32_demo/sketch.ino`, `diagram.json`, `station_config.h`, `gts_root_r4.h` and `HimRakshakModel.h`.
3. Start Serial Monitor at 115200 baud.
4. Change the six potentiometers and press the vibration button.
5. Observe the 16-feature input, TinyML inference, rule-based assessment, final state and local LEDs/buzzer.
6. Test local dashboard scenarios separately; the dashboard scenario controls do not directly manipulate the Wokwi iframe.

**Manual verification still required:** This package's Python tests and native C++ model-header smoke test passed, but the complete ESP32 sketch has not been live-compiled or executed in Wokwi in the packaging environment. Published Wokwi URLs must be supplied by the team before the iframe can display a live circuit.

## Current release boundaries

- No physical sensors or field deployment.
- No physical LoRa implementation.
- No site-calibrated thresholds.
- No scientific/field-validated prediction claims.
- Wokwi proxy measurements must be disclosed.
- External data-source adapters are not the same as live data downloads; source-specific files/credentials and validation are required.
- Firmware HTTPClient requests are synchronous and can briefly block the loop during a network operation; this remains a physical-deployment limitation.

## Historical project notes

Older handoff notes from the original four-input prototype are retained below for project history. Where their descriptions conflict with the current V2 summary above, the V2 summary and release report are authoritative.
