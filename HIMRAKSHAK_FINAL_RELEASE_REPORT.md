# HimRakshak final release report

## Completed phases

| Phase | Description | Status |
| --- | --- | --- |
| 12 | ESP32 Wi-Fi/HTTPS | Implemented in firmware; live Wokwi execution remains manual |
| 13 | Complete cloud integration | Implemented against existing API contract; live cloud verification remains manual |
| 14 | Three independent senders | Implemented with reusable profiles for HR-01/02/03 |
| 15 | Communication reliability | Implemented with firmware FIFO/backoff and existing backend fault model |
| 16 | Offline-first recovery | Preserved and documented for standalone, local, cloud-unavailable, and firmware modes |
| 17 | Final release and documentation | Implemented; automated checks added |

## Architecture

Virtual sensors feed the ESP32/Wokwi firmware, which performs local
illustrative assessment, drives LEDs/buzzer, and sends canonical JSON over
simulated Wi-Fi and TLS HTTPS. The Render Python backend validates schema and
station sequence, recalculates risk, updates one of three independent station
states, and remains the source of truth for the dashboard.

When connectivity is unavailable, the firmware continues local processing and
uses a bounded RAM-only FIFO. The dashboard keeps standalone and backend modes
distinct and reports cloud unavailability instead of mixing browser history
with cloud state.

## Firmware

`wokwi_esp32_demo/sketch.ino` now uses `WiFi.h`, `WiFiClientSecure`, and
`HTTPClient`, a trusted ISRG Root X1 certificate, 10-second telemetry, valid
battery input from a dedicated potentiometer, local risk indicators, a
10-packet FIFO, exponential retry backoff, and serial reliability telemetry.
No administrative key or private credential is embedded.

`sender_profiles/hr01.h`, `hr02.h`, and `hr03.h` select independent station
IDs and demonstration scenarios without duplicating firmware.

## Cloud integration and endpoints

The firmware targets the existing Render route
`/api/stations/{station_id}/readings`. Existing read-only endpoints remain
`/api/health`, `/api/state`, `/api/stations`, and
`/api/stations/{station_id}`. The backend returns structured 409 conflict
metadata for sequence reconciliation and preserves existing 429 rate limiting.

## Testing

Automated local checks include the complete Python unittest suite, the separate
demo acceptance test, firmware contract/static checks, diagram JSON parsing,
JavaScript syntax validation, and `git diff --check`. Verification date:
2026-11-02. The full suite ran 117 tests successfully,
including five firmware contract tests; the separate acceptance test ran
successfully as one test. Inline JavaScript, Wokwi diagram JSON, sender
profiles, TLS certificate markers, secret scanning, and `git diff --check`
also passed. These are local/static checks; Wokwi compilation remains manual.
Wokwi compilation, Wokwi-GUEST networking, TLS delivery, and visual cloud
dashboard refresh remain manual because this environment cannot launch the
hosted simulator.

On 2026-11-02, the existing Render service at
https://himrakshak.onrender.com/ returned HTTP 200 for `/api/health` and
`/api/stations/HR-01`. The Render dashboard/deploy metadata was not available
to this local validation, so the deployed commit was not inferred and the
new cleanup is not claimed as live.

## Known limitations and remaining manual checks

- Wokwi live compilation and Render ingestion need operator verification.
- Firmware queue is in-memory and does not survive reset.
- HTTP 409 responses are reconciled against `GET /api/stations/{node_id}`:
  already-delivered matches are removed, stale packets are explicitly counted
  as superseded, and ambiguous or conflicting packets remain queued.
- Render state is process-memory only and may reset on restart or cold start.
- No physical sensors, SX1262 radio, field validation, or scientific model
  calibration are included.

## Exact run commands

```powershell
python -m himrakshak.server
python -B -m unittest discover -s tests -v
python -B -m unittest tests.test_demo_acceptance -v
python -B -m unittest tests.test_firmware_contract -v
# Extract the inline <script> block, then run:
node --check .\index-inline.js
git diff --check
```

See [`docs/FINAL_DEMO_RUNBOOK.md`](docs/FINAL_DEMO_RUNBOOK.md) for the
operator sequence and limitations.
