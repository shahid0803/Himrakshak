# Live Wokwi dashboard quick start

This update adds a backend command bridge between the landing page and the running HR-01 Wokwi ESP32 simulation.

## 1. Deploy the backend and dashboard

1. Push/deploy the updated repository to the existing Render service using `render.yaml` (`python -m himrakshak.server --allow-remote`).
2. In Render, confirm `HIMRAKSHAK_ADMIN_KEY` has a value. Copy that value from the service environment settings; do not put it into source code or commit it.
3. Set `HIMRAKSHAK_ALLOWED_ORIGINS` to the exact origin hosting the dashboard. For GitHub Pages, use the site's origin, such as `https://account.github.io` (include a custom domain separately if used).
4. Publish the updated `site/` directory. Its `wokwi-config.js` should point to the deployed backend API.
5. Wait for Render to finish deploying before starting the simulator.

The command queue is held in backend process memory. A backend restart clears queued commands and device heartbeat state; send a new command after restart.

## 2. Update the existing Wokwi project

1. Open the existing HR-01 project: <https://wokwi.com/projects/480976780970275841>.
2. Copy the matching firmware files from `wokwi_remote_patch/` into the project, replacing `sketch.ino`, `sensor_manager.h`, `sensor_config.h`, `simulation_scenarios.h`, and `station_config.h`.
3. Add `network_types.h`, `gts_root_r4.h`, and `tinyml_feature_contract.h` if they are not already in that Wokwi project.
4. **Keep the existing `HimRakshakModel.h` unchanged.** It is the model header used by the successful NORMAL / WARNING / CRITICAL tests. The overlay deliberately does not include a replacement.
5. Confirm `station_config.h` uses station `HR-01`, scenario stimulus enabled, remote control enabled, and `HIMRAKSHAK_LOG_LEVEL 1`.
6. Save and start the Wokwi simulation. Leave it running.

The supplied `diagram.json` and `libraries.txt` are references; do not replace a working circuit/library setup unnecessarily.

## 3. Control the running ESP32 from the landing page

1. Open the published landing page and select station `HR-01` in **Multi-Parameter Geotechnical Intelligence**.
2. Paste the Render `HIMRAKSHAK_ADMIN_KEY` into the password field and press **Save for this tab**. It is held in browser `sessionStorage` for that tab only.
3. Wait for the badge to show **Wokwi online**. The firmware polls the backend every 3 seconds; the online badge uses a 20-second heartbeat window.
4. Use the **Send a scenario to Wokwi** buttons or adjust the 14 **Live Wokwi remote control** sliders and press **Apply sensor values to running Wokwi ESP32**.
5. The command should change from queued to acknowledged after the ESP32 applies it. New ESP32 telemetry is sent at approximately 10-second intervals and the dashboard refreshes approximately every 4 seconds.

Use the **Browser-local preview controls** only for the old in-browser preview. They do not control Wokwi. The separately labelled **Backend-only preview** buttons also do not control Wokwi.

## 4. What the control changes mean

The sliders change the ESP32's internal virtual sensor stimulus, which is then processed by the existing SensorManager, 16-feature contract, TinyML Decision Tree, and independent safety rules. The controls do not physically or graphically move Wokwi's potentiometer knobs. One-hour/24-hour rainfall totals, tilt rate, and displacement rate are derived over time by the firmware rather than set directly by a slider.

The default Serial Monitor mode (`HIMRAKSHAK_LOG_LEVEL 1`) prints boot and network information, state/classification changes, remote command receipt/acknowledgement, and concise telemetry messages. It suppresses repeated full feature vectors and per-sample diagnostics. Set the level to `2` temporarily only when debugging.

## 5. Troubleshooting

- **Wokwi offline:** ensure the simulator is running, `HR-01` is selected, and Wi-Fi/NTP/backend sequence synchronization succeeded.
- **Command stays queued:** check the browser admin key, backend deployment, Wokwi's backend URL/certificate, and the device's compact `[REMOTE]` messages.
- **Dashboard has no Wokwi readings:** wait for the first telemetry POST; the device sends a frame approximately every 10 seconds. A recent poll heartbeat alone does not mean telemetry delivery succeeded.
- **CORS error:** set `HIMRAKSHAK_ALLOWED_ORIGINS` to the exact dashboard origin and redeploy the backend.

This is a hackathon software simulation using synthetic stimuli and synthetic training labels. It is not field-validated, production device authentication, or a safety warning system. The Wokwi source project is public; the command acknowledgement/heartbeat is demo-grade rather than cryptographically authenticated per-device.
