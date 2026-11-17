# Embedded firmware and sensors

The Wokwi circuit uses an ESP32 DevKit v1, six ADC1 potentiometers, a
pushbutton, three LEDs, and a piezo buzzer. ADC1 pins are used so the virtual
inputs remain compatible with Wi-Fi.

## Pin map

| Input/output | GPIO | Meaning |
|---|---:|---|
| Rainfall potentiometer | 33 | Intensity proxy |
| Surface moisture potentiometer | 34 | Surface moisture |
| Tilt potentiometer | 35 | Tilt-X proxy |
| Soil temperature potentiometer | 32 | Temperature proxy |
| Subsurface moisture potentiometer | 36 | Subsurface moisture |
| Pore-pressure potentiometer | 39 | Pressure proxy |
| Vibration button | 18 | Discrete virtual event |
| Green/yellow/red LEDs | 25/26/27 | Normal/warning/critical |
| Buzzer | 23 | Local alert pattern |

Battery is a fixed demonstration health estimate, not an ADC input or TinyML
feature. The full feature order is maintained in `himrakshak/tinyml.py` and
`makeTinyMlFeatures()` in the sketch.

## Derived and virtual values

- Rainfall history is seeded and accelerated; one real second represents one
  simulated minute.
- Tilt-Y is a documented cross-axis proxy.
- Displacement and displacement rate are calculated from a demonstration
  lever-arm model and successive samples.
- Vibration RMS, peak, and frequency are a button-event template, not sampled
  accelerometer or FFT data.
- Soil temperature is potentiometer-derived, not a DS18B20 reading.
- Groundwater depth is calculated from pressure and subsurface moisture, not a
  well transducer.

## Runtime behavior

At boot the sketch reports initialization and the TinyML model banner. Each
loop samples and filters inputs (`alpha = 0.25`), checks for invalid or stuck
inputs, assesses the local rule state, updates indicators, and attempts
telemetry. The RAM queue is bounded at 10 packets, uses retry/backoff, and is
not persistent across reset. HTTP operations are synchronous and can briefly
block the loop.

Critical, warning, and degraded-communication states use distinct indicator
patterns. Local assessment continues while Wi-Fi is unavailable.

## Firmware files

Upload `sketch.ino`, `diagram.json`, `station_config.h`, `gts_root_r4.h`, and
`HimRakshakModel.h` from `wokwi_esp32_demo/`. Copy a profile from
`sender_profiles/` over `station_config.h` for HR-01, HR-02, or HR-03.
The [Wokwi guide](wokwi-guide.md) provides the complete procedure.
