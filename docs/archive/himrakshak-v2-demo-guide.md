# HimRakshak V2 judge demonstration guide

## What this version demonstrates

HimRakshak V2 combines:

- Eight environmental parameter groups represented by 16 numerical model features.
- A virtual ESP32 acquisition sketch with six ADC1 potentiometer channels and a button event input.
- Derived/seeded virtual proxies for rainfall windows, second-axis tilt, displacement, vibration statistics, temperature, and groundwater depth.
- A compact Decision Tree trained on a reproducible synthetic scenario dataset and exported as C++.
- Backend V2 schema validation, server-side model inference, rule-based safety assessment, and conservative severity fusion.
- Three independent backend stations: HR-01, HR-02, and HR-03.

This is a prototype demonstration. Some features in Wokwi are proxies rather than separate physical instruments. The model has not been field-validated.

## Start

From the repository root:

```powershell
python -m himrakshak.server
```

Open `http://127.0.0.1:8765/`.

The **Multi-Parameter Geotechnical Intelligence** panel calls the local backend. Pick a station and run one of the V2 scenario buttons.

## Four scenario walkthrough

1. **Normal** – low rainfall, lower moisture and pressure, small tilt, low vibration. Expected backend assessment: NORMAL.
2. **Heavy rain** – elevated rainfall, moisture and pore pressure. Expected backend assessment: WARNING.
3. **Slope shift** – high tilt, tilt rate, displacement rate, pore pressure and accumulated rainfall. Expected backend assessment: CRITICAL.
4. **Vibration burst** – event-style vibration features with otherwise baseline values. Expected backend assessment: WARNING.

The expected TinyML outputs are NORMAL=0, Heavy rain=1, Slope shift=2, and Vibration burst=1. The vibration-burst training subtype represents an inspection/anomaly warning over otherwise baseline simulated inputs; it is not a prediction of slope failure. The server recomputes the Decision Tree prediction from the validated V2 feature vector. It does not trust the device-provided ML prediction. The final assessment uses the more severe of the model class and rule-based class; the model cannot suppress a rule-based warning/critical result.

## V2 feature order

The order is fixed in `himrakshak/tinyml.py` and mirrored in firmware `makeTinyMlFeatures()`:

1. rainfall_intensity_mm_h
2. rainfall_1h_mm
3. rainfall_24h_mm
4. soil_moisture_surface_pct
5. soil_moisture_subsurface_pct
6. pore_water_pressure_kpa
7. tilt_x_deg
8. tilt_y_deg
9. tilt_rate_deg_per_h
10. ground_displacement_mm
11. ground_displacement_rate_mm_per_h
12. vibration_rms_g
13. vibration_peak_g
14. vibration_dominant_hz
15. soil_temperature_c
16. groundwater_depth_m

Battery, Wi-Fi RSSI, communication status and sensor-health status are not ML features.

## Re-train and export

```powershell
python -m pip install -r requirements-tinyml.txt
python train_tinyml.py --generate
```

The command updates:

- `data/processed/himrakshak_synthetic_v2.csv`
- `models/himrakshak_tinyml_v2.json`
- `models/training_metrics_v2.json`
- `wokwi_esp32_demo/HimRakshakModel.h`

Current test metrics are based on a seeded synthetic dataset with scenario-derived labels. Do not present the test accuracy or recall as real-world landslide-prediction accuracy.

## API

```powershell
# Model and feature contracts
Invoke-RestMethod http://127.0.0.1:8765/api/v2/model
Invoke-RestMethod http://127.0.0.1:8765/api/v2/features

# Run a deterministic scenario
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8765/api/v2/demo `
  -ContentType 'application/json' `
  -Body '{"station_id":"HR-01","scenario":"slope_shift"}'
```

## Wokwi setup

Upload `sketch.ino`, `diagram.json`, `station_config.h`, `gts_root_r4.h`, and `HimRakshakModel.h` into a Wokwi ESP32 project. Start Serial Monitor at 115200 baud. Official Wokwi project URLs remain configured manually in `wokwi-config.js`.

The Wokwi iframe is not controlled by the local V2 scenario buttons. The local backend scenarios are separate synthetic demonstration controls. Wokwi-to-dashboard connection in the current design is manual packet copy/paste or HTTPS telemetry, not direct iframe control.

## Before presenting

- Compile and run the sketch in Wokwi; this package has not been live-compiled by Wokwi in the local packaging environment.
- Check all six potentiometers, button, LEDs and buzzer.
- Confirm `[TINYML INITIALIZED]` and `[TINYML INFERENCE]` logs.
- Test the local backend scenario buttons.
- Explain which features are measured virtual inputs and which are derived proxies.
- State plainly that model metrics are synthetic-label agreement and field validation has not occurred.
