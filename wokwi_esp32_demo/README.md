# Virtual ESP32 autonomous edge controller for HimRakshak

This companion project demonstrates the software path from simulated ESP32
inputs to the HimRakshak API. It uses Wokwi's simulated `Wokwi-GUEST` network
and HTTPS. It does not use physical sensors, physical LoRa, or authenticated
device credentials. The public ingestion endpoint is a temporary academic
demonstration endpoint.

## Open it in Wokwi

1. Visit https://wokwi.com/esp32 and create an ESP32 project.
2. Replace the sketch with `sketch.ino` and the circuit with `diagram.json`.
3. Copy one sender profile over `station_config.h` (HR-01 is the default).
4. Start the simulation and open Serial Monitor at 115200 baud.
5. Turn the moisture, tilt, and battery potentiometers; press the blue button
   for a vibration event.
6. Observe local state transitions and the LEDs/buzzer. Wi-Fi startup and NTP
   readiness are polled without long blocking waits; local sampling begins
   immediately.
7. Once Wi-Fi and NTP are ready, the sketch reads the station's latest accepted
   sequence and then sends queued telemetry to
   `/api/stations/{station_id}/readings` over certificate-verified HTTPS.
8. Observe HTTP status, sequence, queue depth, retries, and local indicators.
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
| `hr02.h` | HR-02 | Heavy-rain preset plus live inputs |
| `hr03.h` | HR-03 | Slope-shift preset plus live inputs |

Each profile is an independent virtual sender. Do not run two senders with the
same station ID. If packets are queued while offline, the sender checks the
backend's latest accepted sequence after connectivity and clock synchronization
return. Queued packet sequences are rebased before delivery when needed.

## Control mapping

- Potentiometer 1 -> soil moisture (0-100%)
- Potentiometer 2 -> tilt (0-15 degrees)
- Potentiometer 3 -> battery input (0-100%)
- Blue pushbutton -> vibration event (85 while pressed, 12 otherwise)
- Green LED -> Normal
- Yellow LED -> Warning; blinking yellow -> Sensor fault
- Red LED -> Critical
- Piezo buzzer -> distinct warning, critical, and sensor-fault patterns

Environmental risk state and communication health are separate. Losing Wi-Fi
must not overwrite the local Normal/Warning/Critical/Sensor-fault decision or
turn off an active critical alert. If a sensor fault occurs while a critical
alert is already active, red remains active and yellow also blinks to show the
sensor fault.

## Deliberate sensor-fault demonstration

The sketch does not classify an unchanged potentiometer value as a failed
sensor. A healthy potentiometer can remain at one position for a long time.
The full 0-4095 ADC range is valid for the Wokwi potentiometers.

To deliberately demonstrate `SENSOR_FAULT`, add this line to the active
`station_config.h` after copying the desired sender profile:

```cpp
#define HIMRAKSHAK_SIMULATE_SENSOR_FAULT 1
```

Set it back to `0` (or remove the line) after the fault demonstration. This is
a software fault-injection switch, not proof that a physical sensor has been
diagnosed or disconnected.

## Firmware behaviour

- Analog moisture, tilt, and battery inputs are filtered using an exponential
  moving average (`FILTER_ALPHA`).
- The discrete vibration pushbutton is not smoothed, so short events remain
  visible to the decision engine.
- Critical hysteresis is checked before a downgrade to Warning.
- Environmental state and network state are tracked separately.
- The local indicators are updated before network servicing in each loop.
- Diagnostics are rate-limited to approximately one summary per second.
- Telemetry is built from the same filtered `SensorFrame` used by the local
  decision engine.
- Startup Wi-Fi and NTP checks do not use long `while`/`delay` waits.

HTTPS and station-state requests still use the synchronous Arduino
`HTTPClient` API. A request timeout can briefly pause the main loop. This is
acceptable for the virtual hackathon demonstration, but a real-time physical
deployment should move network I/O into a separate FreeRTOS task and add
hardware-specific sensor diagnostics.

## Packet and reliability behaviour

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
only and is lost on an ESP32 reset. HTTPS certificate verification remains
enabled through `HIMRAKSHAK_ROOT_CA`; `setInsecure()` must not be used.

Wokwi simulates ESP32 logic, inputs, LEDs, buzzer, Wi-Fi, and HTTPS only. It
does not simulate an SX1262 radio or prove LoRa packet delivery/range. Risk
thresholds are illustrative and have not been validated for real slopes.

## Local contract checks

From the repository root:

```powershell
python -B -m unittest tests.test_firmware_contract -v
```

This local test checks code contracts and the Wokwi diagram. It does not claim
live Wokwi compilation, Wi-Fi connectivity, or cloud delivery.
