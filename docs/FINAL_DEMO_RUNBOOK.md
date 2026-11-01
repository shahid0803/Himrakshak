# HimRakshak final demonstration runbook

## 1. Purpose and disclosure

HimRakshak is an offline-first, software-only landslide monitoring
demonstration. All readings, risk scores, Wi-Fi, HTTPS, and recovery behavior
are simulated or software-controlled. It is not a certified warning system,
does not measure a real slope, and does not provide physical LoRa
communication.

## 2. Final architecture

```text
Virtual sensors
  -> ESP32/Wokwi firmware
  -> Wokwi-GUEST Wi-Fi
  -> HTTPS POST /api/stations/{id}/readings
  -> Render Python backend
  -> schema + sequence validation
  -> demonstration risk engine
  -> independent HR-01/HR-02/HR-03 state
  -> cloud dashboard polling every 5 seconds
```

Offline path:

```text
Virtual sensors
  -> ESP32 local assessment and LEDs/buzzer
  -> bounded in-memory pending queue
  -> controlled retry after connectivity returns
```

Wokwi Wi-Fi/HTTPS is not SX1262 or physical LoRa communication.

## 3. Technology stack

The backend is Python standard library only. The dashboard is standalone
HTML/CSS/vanilla JavaScript. Wokwi runs Arduino ESP32 firmware with Wi-Fi and
`WiFiClientSecure`; Render supplies the existing public cloud API.

## 4. Start the dashboard

Cloud: open the deployed dashboard URL and confirm `CLOUD BACKEND CONNECTED`.
Local: run `python -m himrakshak.server`, then open
`http://127.0.0.1:8765/` and confirm `LOCAL BACKEND CONNECTED`.
Standalone: open `index.html` directly and confirm
`STANDALONE SIMULATION`.

The browser never silently substitutes local history for unavailable cloud
history.

## 5. Start HR-01, HR-02, and HR-03

For each sender, create or reuse an ESP32 Wokwi project, copy the common
`sketch.ino` and `diagram.json`, then copy the matching profile:

```powershell
Copy-Item sender_profiles\hr01.h station_config.h
# run HR-01
Copy-Item sender_profiles\hr02.h station_config.h
# run HR-02
Copy-Item sender_profiles\hr03.h station_config.h
# run HR-03
```

Each running project is an independent sender. Keep the 10-second telemetry
interval and use the three station IDs exactly as shown.

## 6. Normal and warning scenarios

HR-01 starts with live potentiometers. HR-02 uses the heavy-rain preset and
HR-03 uses the slope-shift preset while still reading the virtual inputs.
Adjust moisture, tilt, battery, and the vibration button to demonstrate local
indicator changes. The backend recalculates risk and ignores incoming risk
labels.

## 7. Failure and recovery demonstration

Disconnect a sender's simulated Wi-Fi or stop its network access. The firmware
continues local assessment and queues up to ten validated packets. Serial
Monitor reports queue depth, retries, last error, and delivery counts. Restore
Wi-Fi and observe FIFO delivery with exponential backoff.

For deterministic local API reliability controls, run the local backend and
use the existing `/api/stations/{id}/faults/*` routes. Remote fault routes are
administratively protected and must not receive the admin key from browser or
firmware code.

## 8. Expected endpoints

- `GET /api/health`: service metadata and `station_count: 3`
- `GET /api/state`: legacy HR-01-compatible latest state and global events
- `GET /api/stations`: all station summaries
- `GET /api/stations/HR-01` (or HR-02/HR-03): station history and communication
- `POST /api/stations/{id}/readings`: public validated telemetry ingestion

Successful ingestion returns `reading` and backend `assessment`. Duplicate or
stale sequences return HTTP 409 with `classification`, `sequence`, and
`latest_sequence`. Rate limiting returns HTTP 429 with `Retry-After`.

## 9. Local/offline checks

```powershell
python -m himrakshak.server
python -B -m unittest discover -s tests -v
python -B -m unittest tests.test_demo_acceptance -v
python -B -m unittest tests.test_firmware_contract -v
```

Open `index.html` without a server to verify standalone mode. Stop or block
the cloud backend to verify `CLOUD BACKEND UNAVAILABLE`, then restore it and
observe automatic polling recovery.

## 10. Known limitations and status

Implemented and automated-tested: backend contracts, station isolation,
sequence preservation, risk recalculation, dashboard polling states, firmware
packet contract checks, diagram JSON validation, and local reliability model.

Manually verified by the operator: Wokwi compilation, Wokwi-GUEST networking,
TLS handshake, live Render ingestion, three simultaneous sender runs, and
visual dashboard updates. These require the Wokwi and cloud environments and
are not claimed by local tests.

Not physically validated: real sensors, physical ESP32 boards, SX1262 LoRa,
radio range, field deployment, persistence across restart, or scientific
landslide prediction.

## 11. Suggested presentation flow

1. Show standalone offline mode and explain the disclosure.
2. Start local backend and show the three-station roster.
3. Start HR-01 and show accepted telemetry and risk.
4. Start HR-02 and HR-03 to show independent state.
5. Trigger heavy rain or slope shift and show backend-calculated risk.
6. Disconnect HR-02, show queue and local warning, then reconnect.
7. Finish with cloud/unavailable recovery and the API contract.
