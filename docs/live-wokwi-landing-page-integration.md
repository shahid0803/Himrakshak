# Live Wokwi ↔ HimRakshak dashboard integration

## What this adds

- A landing-page panel for setting 14 virtual sensor stimulus values or sending a named scenario.
- A versioned backend command queue at `/api/stations/{station}/simulation-command`.
- ESP32 polling every 3 seconds, application through `ScenarioManager` and `SensorManager`, and a command acknowledgement.
- Actual Wokwi telemetry continues to POST to `/api/stations/{station}/readings`; the dashboard reads the latest station detail and labels it as Wokwi telemetry only when `transport` is `WOKWI_SOFTWARE_SIMULATION`.
- Serial Monitor compact mode by default. Set `HIMRAKSHAK_LOG_LEVEL` to `2` in `station_config.h` for diagnostic detail.

## Architecture and data truth

The Wokwi iframe is a cross-origin page. The landing page cannot directly read or manipulate Wokwi's internal controls. Instead, both the dashboard and the running ESP32 communicate with the Python backend: dashboard → authenticated command endpoint → ESP32 polls → virtual sensor stimulus → ESP32 TinyML/rules → HTTPS telemetry endpoint → dashboard. The Wokwi simulation must remain running and connected to Wi-Fi for commands to be applied.

This is a remote virtual stimulus interface. It changes the ESP32's internal simulated sensor source; it does **not** move the graphical potentiometer knobs in the Wokwi iframe. The hardware diagram remains available for visual context.

The command fields are engineering-unit virtual inputs: rainfall intensity (mm/h), surface and subsurface moisture (%), pore pressure (kPa), X/Y tilt (degrees), ground displacement (mm), vibration RMS (g), vibration frequency (Hz), soil temperature (°C), groundwater depth (m), air temperature (°C), humidity (%), and atmospheric pressure (hPa). The current ESP32 feature contract remains exactly 16 ordered model inputs; derived fields are not writable dashboard controls. In particular, 1-hour / 24-hour rainfall accumulations, tilt rate and displacement rate are calculated by the firmware from the evolving signal/history and will not jump instantly when a slider moves.

The model and its training dataset are synthetic demonstration assets, not field validated. TinyML prediction is the on-device model result; backend model prediction is displayed separately as a comparison. Do not treat their agreement as field accuracy.

## Wokwi firmware overlay

The `wokwi_remote_patch/` folder is an overlay for the newer modular Wokwi project which was tested with the 50 Hz scenario-mode sampling fix. Copy its files into the existing Wokwi project, replacing same-named files. Add any missing overlay files. **Do not replace `HimRakshakModel.h`**: preserve the existing model header whose metadata hash is `a54175fdad7bb68f249e58fed707437aa6cf0f0c3d1ba5cd27941a8fd40db412`. The overlay intentionally excludes that header.

Keep `station_config.h` set to `HR-01`, `HIMRAKSHAK_USE_SCENARIO_PRESET 1`, and `HIMRAKSHAK_REMOTE_CONTROL_ENABLED 1`. The recommended log level is `1` (compact). The remote command code uses `https://himrakshak.onrender.com`, certificate-verified TLS and the existing trusted root certificate. If you deploy the backend at a different host, update `INGEST_URL` in `sketch.ino` and use the matching root CA certificate.

## Dashboard configuration

Both `index.html` and `site/index.html` load `wokwi-config.js`. The supplied configuration sets:

- API base: `https://himrakshak.onrender.com`
- HR-01 Wokwi iframe: `https://wokwi.com/projects/480976780970275841?embed=1`

The HR-01 URL comes from the Wokwi project URL supplied for this project. Confirm the Wokwi project is public and that the iframe is allowed by the browser. HR-02 and HR-03 remain unconfigured.

For local backend development, set the API base to `http://127.0.0.1:8765` (or empty if the page is served by the same backend origin).

## Deploy the backend

1. Deploy the modified repository using the existing Render service definition (`python -m himrakshak.server --allow-remote`).
2. In the Render service environment, ensure `HIMRAKSHAK_ADMIN_KEY` is configured. `render.yaml` generates a value for new service creation; copy the active value from the service dashboard.
3. Set `HIMRAKSHAK_ALLOWED_ORIGINS` to the exact published dashboard origin(s), comma-separated. For example, use the `https://<account>.github.io` origin for a GitHub Pages site (include a custom domain if one is used). The default code value `*` is convenient for initial testing but should be restricted for a deployed demo.
4. Publish the modified static site folder (`site/`) and make sure its `wokwi-config.js` points to the deployed API. The root `index.html` is also updated for the Python server's same-origin mode.
5. Do not put the admin key into `wokwi-config.js`, source code, a GitHub commit, or Wokwi firmware. Enter it into the password field in the dashboard; it is kept in `sessionStorage` for the current browser tab only.

The command-creation endpoint requires `X-HimRakshak-Admin` in remote mode. Device poll/ack and telemetry routes follow the existing demo sender contract. This is a hackathon/demo bridge, not production device authentication; production deployments need per-device credentials and stricter command authorization.

## Run the live demo

1. Start the HR-01 Wokwi simulation and leave it running. Wait for Wi-Fi connection and NTP synchronization.
2. Open the landing page, choose station `HR-01`, and enter the Render admin key if the backend is in remote mode.
3. Wait until the device badge says `Wokwi online`. The firmware polls command updates every 3 seconds; the online indicator uses a 20-second heartbeat window.
4. Move the remote sliders and click **Apply sensor values to running Wokwi ESP32**, or click one of the live scenario buttons. The page reports the versioned command as queued, then acknowledged after the ESP32 applies it.
5. The firmware continues to process the stimulus locally. Telemetry is sent about every 10 seconds; the dashboard refreshes the station detail every 4 seconds and shows device TinyML, backend comparison and rule result separately.
6. If the Wokwi badge is offline, start/restart the simulation, confirm it is using `HR-01`, and check the backend URL/certificate, Wi-Fi and NTP status.

## Short Serial Monitor

`HIMRAKSHAK_LOG_LEVEL 1` is compact mode. It emits boot, network/state changes, TinyML classification changes, remote command receipt/acknowledgement, and concise telemetry status rather than repeated full feature vectors. Use level `2` temporarily when debugging. Keep level `1` for the demonstration; the full sensor vectors and repeated diagnostic lines are disabled in this mode.

## Validation boundaries

The control API validates sensor value ranges and stores commands per station in the server process memory. Commands are not durable across a backend restart. The polling heartbeat confirms that a Wokwi firmware instance requested commands recently; an acknowledgement confirms that it applied a version. It is not proof of physical measurement or real-world safety.
