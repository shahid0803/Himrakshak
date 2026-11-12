# Virtual ESP32 cloud sender for HimRakshak

This companion project demonstrates the software path from simulated ESP32
inputs to the deployed HimRakshak API. It uses Wokwi's simulated
`Wokwi-GUEST` network and HTTPS; it does not simulate physical LoRa or field
sensors.

## Open it in Wokwi

1. Visit https://wokwi.com/esp32 and create an ESP32 project.
2. Replace the sketch with `sketch.ino` and the circuit with `diagram.json`.
3. Copy one sender profile over `station_config.h` (HR-01 is the default).
4. Start the simulation and open Serial Monitor at 115200 baud.
5. Turn the moisture, tilt, and battery potentiometers; press the blue button
   for a vibration burst.
6. Wait at least 10 seconds between telemetry packets. The sketch connects to
   `Wokwi-GUEST`, validates the Google Trust Services GTS Root R4 certificate
   used by Render's current chain, synchronizes NTP before its startup HTTPS
   state read, and POSTs
   to `/api/stations/{station_id}/readings`.
7. Observe HTTP status, sequence, queue depth, retries, and local indicators.
   The public dashboard refreshes accepted backend state every five seconds.

## Three independent senders

The reusable sketch uses `station_config.h`. Copy one profile before each
independent Wokwi run:

```powershell
Copy-Item sender_profiles\hr01.h station_config.h
# or sender_profiles\hr02.h / sender_profiles\hr03.h
```

| Profile | Station | Default demonstration |
| --- | --- | --- |
| `hr01.h` | HR-01 | Normal slope, live inputs |
| `hr02.h` | HR-02 | Heavy rain preset plus live inputs |
| `hr03.h` | HR-03 | Slope-shift preset plus live inputs |

Each profile is an independent virtual sender. On startup it reads the station
state when possible and advances its next sequence beyond the backend's latest
accepted device sequence; it preserves a higher locally retained sequence.
Do not run two senders with the same station ID.

## Control mapping

- Potentiometer 1 -> soil moisture (0-100%)
- Potentiometer 2 -> tilt (0-15 degrees)
- Potentiometer 3 -> battery input (0-100%)
- Blue pushbutton -> vibration burst (85 while pressed, 12 otherwise)
- Green/yellow/red LEDs -> Normal/Warning/Critical local assessment
- Piezo buzzer -> beeps during the Critical local state

## Packet and reliability behavior

Packets use the canonical fields `node_id`, `sequence`, `timestamp`,
`soil_moisture_pct`, `tilt_deg`, `vibration_level`, and `battery_pct`, plus
non-authoritative display metadata and the honest
`WOKWI_SOFTWARE_SIMULATION` transport marker. The backend recalculates risk.

Packets are held in a bounded in-memory FIFO of ten entries. Wi-Fi failures,
timeouts, 429 responses, and ambiguous errors use exponential backoff. A
structured 409 response triggers `GET /api/stations/{station_id}`: a matching
sequence and reading is counted as already delivered; an older packet is
explicitly counted and warned as superseded; newer, disconnected, failed-GET,
or same-sequence/different-reading conflicts remain queued. The queue is RAM
only and is lost on an ESP32 reset. Serial output exposes connection state,
attempts, deliveries, already-delivered and superseded counters, retries,
queue depth, and the last error without printing credentials.

Wokwi simulates ESP32 logic, inputs, LEDs, buzzer, Wi-Fi, and HTTPS only. It
does not simulate an SX1262 radio or prove LoRa packet delivery/range. The
Render endpoint is temporary academic demonstration infrastructure; no
administrative key is embedded in firmware. Risk thresholds are illustrative
and have not been validated for real slopes.

If Wokwi cannot be run, validate the packet contract and diagram locally with:

```powershell
python -B -m unittest tests.test_firmware_contract -v
```

That local check does not claim live Wokwi Wi-Fi or cloud delivery.
