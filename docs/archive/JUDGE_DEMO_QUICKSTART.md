# HimRakshak V2 – Judge Demo Quick Start

This package demonstrates an eight-group virtual geotechnical monitoring feature set, a 16-feature TinyML Decision Tree, backend V2 assessment, and ESP32 local inference. It is a software/virtual-hardware prototype, not a field-deployed landslide warning device.

## 1. Start the website and backend

From this folder in PowerShell:

```powershell
python -m himrakshak.server
```

Open `http://127.0.0.1:8765/`.

In the **Multi-Parameter Geotechnical Intelligence** panel, select a station and try:

| Demo button | Expected TinyML class | Expected final state |
|---|---:|---|
| Normal | 0 | NORMAL |
| Heavy rain | 1 | WARNING |
| Slope shift | 2 | CRITICAL |
| Vibration burst | 1 | WARNING |

The dashboard scenario controls invoke the local Python backend. They do not remotely control the Wokwi iframe.

## 2. Run the TinyML model training again (optional)

The checked-in model and generated C++ header are already included. To regenerate them:

```powershell
python -m pip install -r requirements-tinyml.txt
python train_tinyml.py --generate
```

This uses 2,400 synthetic scenario examples. Current holdout accuracy is 98.83% and synthetic critical-class recall is 99.5%. These numbers measure agreement with synthetic labels only; they are not real-world landslide prediction accuracy.

## 3. Run tests

```powershell
python -B -m unittest discover -s tests -v
python -m compileall -q himrakshak tests scripts
```

The packaged validation run passed 152 tests.

## 4. Open the Wokwi virtual ESP32 project

Create/open an ESP32 DevKit V1 project at Wokwi and upload:

- `wokwi_esp32_demo/sketch.ino`
- `wokwi_esp32_demo/diagram.json`
- `wokwi_esp32_demo/station_config.h`
- `wokwi_esp32_demo/gts_root_r4.h`
- `wokwi_esp32_demo/HimRakshakModel.h`

For a station profile, copy one profile over `station_config.h` before upload:

```powershell
Copy-Item wokwi_esp32_demo/sender_profiles/hr01.h wokwi_esp32_demo/station_config.h -Force
# Use hr02.h for the synthetic heavy-rain profile, or hr03.h for slope-shift.
```

Start the simulation and open Serial Monitor at **115200 baud**. Confirm the `[TINYML INITIALIZED]` message, then adjust the six potentiometers and press the vibration button. Check the prediction, final controller state, LEDs and buzzer.

The current diagram contains six potentiometer proxies, one vibration-event button, three LEDs and a buzzer. It does not contain calibrated field instruments. Rainfall windows are seeded/accelerated history; tilt-Y, displacement, vibration statistics, temperature, and groundwater are derived or scripted proxies as documented in `wokwi_esp32_demo/README.md`.

**Manual verification required:** this package was not compiled or run in the hosted Wokwi environment or with the official Arduino ESP32 toolchain during local packaging. Confirm the actual Wokwi compile and run before presenting the circuit as live.

## 5. Publish the simulator for the landing-page embed

Publish each Wokwi project and copy its real official embed URL into `wokwi-config.js` for HR-01, HR-02 and HR-03. Do not invent IDs. The site includes a fallback until URLs are supplied.

## 6. What to say about TinyML

The embedded Decision Tree uses 16 ordered environmental features. Battery, communications and sensor health are separate device-health inputs. The backend recomputes model inference from validated features; device-supplied predictions are retained for comparison only. The safety fusion can raise severity but never lowers an existing rule-based warning or critical state.

The training data is synthetic and generated from illustrative prototype scenarios. The model demonstrates training, export, and local inference mechanics. It has not been validated against real Himalayan slope events and must not be used for evacuation, public warning, or other life-safety decisions.
