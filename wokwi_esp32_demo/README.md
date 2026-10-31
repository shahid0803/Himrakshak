# Virtual ESP32 sensor node for HimRakshak

This companion project lets the team test the input-to-rule-to-indicator logic in Wokwi before buying hardware.

## Open it in Wokwi

1. Visit https://wokwi.com/esp32 (internet is needed to open the online simulator).
2. Create an ESP32 project.
3. Replace the starter sketch with `sketch.ino` from this folder.
4. Replace the project `diagram.json` with the included `diagram.json`.
5. Start the simulation and open Serial Monitor at 115200 baud.
6. Turn the first potentiometer to change simulated moisture; turn the second to change simulated tilt; press the blue button to create a vibration burst.
7. Observe the green/yellow/red LEDs and piezo buzzer. Copy one complete JSON line from Serial Monitor.
8. Open `../index.html`, paste that JSON line into the packet-import field, and click **Import virtual ESP32 packet**.

## Control mapping

- Potentiometer 1 → simulated soil moisture (0–100)
- Potentiometer 2 → simulated tilt signal (0–15 degrees)
- Blue pushbutton → vibration burst (85 while pressed, 12 otherwise)
- Green / yellow / red LEDs → Normal / Warning / Critical demo state
- Piezo buzzer → beeps during the Critical demo state

## Example output schema

The sketch prints one JSON object per line using the same names as the dashboard and Member 2's proposed data contract: `node_id`, `sequence`, `timestamp`, `soil_moisture_pct`, `tilt_deg`, `vibration_level`, `battery_pct`, `risk`, `risk_score_demo`, and `transport`.

## Limitations

Wokwi simulates ESP32 logic, inputs, LEDs and the buzzer. This project does **not** simulate an SX1262 radio or prove LoRa packet delivery/range. Importing the packet into the dashboard is manual copy/paste, not a live connection. Risk thresholds are for demonstration only and have not been validated for real slopes.
