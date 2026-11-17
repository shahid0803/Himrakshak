# Wokwi setup and demonstration

This guide runs the virtual ESP32 path. It does not claim physical sensors,
physical LoRa, or live field measurements.

## Upload and configure

1. Open an ESP32 DevKit project at [Wokwi](https://wokwi.com/).
2. Upload `sketch.ino`, `diagram.json`, `station_config.h`, `gts_root_r4.h`,
   and `HimRakshakModel.h` from `wokwi_esp32_demo/`.
3. For each station, copy a profile before uploading:

   ```powershell
   Copy-Item sender_profiles\hr01.h station_config.h -Force
   ```

   Use `hr02.h` and `hr03.h` for the other stations.
4. Start the simulation and open Serial Monitor at 115200 baud.
5. Confirm `[TINYML INITIALIZED]`, then inspect feature, inference, and state
   transition logs.

The pin mapping and proxy disclosure are in
[embedded and sensors](embedded-and-sensors.md).

## Demonstration sequence

1. Leave the controls near baseline and observe NORMAL plus the green LED.
2. Increase moisture or pressure and observe filtered local changes.
3. Increase tilt to demonstrate WARNING/CRITICAL thresholds.
4. Press the vibration button and observe the event template and alert.
5. Drive an ADC input to an invalid rail to inspect sensor diagnostics.
6. Disconnect simulated Wi-Fi, verify local indicators continue and the
   bounded queue reports pending packets.
7. Restore Wi-Fi and observe retry/backoff delivery.

The local dashboard scenario controls are separate from the Wokwi iframe; they
do not remotely manipulate Wokwi controls. Wokwi HTTPS delivery also requires
an operator-configured backend and cannot be inferred from local tests.

## Website embedding

Publish each station project and enter its official HTTPS URL in
`wokwi-config.js`. Empty entries intentionally show setup guidance. Never
invent project IDs or describe a URL as live until it has been opened and
verified.

## Pre-demo verification

Compile and run the complete sketch in Wokwi, verify all six potentiometers,
button, LEDs, buzzer, Wi-Fi/TLS behavior, and the dashboard packet path.
Those are manual checks; the repository test suite validates contracts and
JSON/configuration but not hosted Wokwi execution.
