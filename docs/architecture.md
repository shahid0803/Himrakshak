# Architecture

HimRakshak is an autonomous embedded-system demonstration. Virtual inputs
feed an ESP32 controller, which validates and filters them, evaluates
illustrative rules, drives local indicators, and optionally delivers telemetry
over certificate-verified HTTPS.

```text
virtual sensors
  -> ADC acquisition and filtering
  -> sensor-health checks
  -> local rule engine
  -> autonomous state machine
  -> LEDs/buzzer
  -> Wi-Fi/HTTPS
  -> bounded retry queue
  -> Python API
  -> schema validation and server-side inference
  -> station state and dashboard
```

## Component boundaries

- **Firmware:** acquires virtual inputs, performs local assessment, controls
  indicators, and queues telemetry when communication is unavailable.
- **Backend:** validates and normalizes packets, owns accepted station state,
  rejects duplicate/stale sequences, recalculates assessment, and exposes the
  dashboard API.
- **Dashboard:** observes backend state in connected mode or runs a clearly
  labelled standalone browser simulation from `index.html`.
- **Data intelligence:** is a separate provenance-aware research-data layer.
  It does not silently turn simulated readings into training observations.

HR-01, HR-02, and HR-03 have independent simulator state, sequence handling,
latest readings, communication state, counters, and bounded histories. Backend
state is in memory unless the optional intelligence SQLite path is configured.

The local state machine includes `BOOT`, `INITIALIZING`, `NORMAL`, `WARNING`,
`CRITICAL`, `SENSOR_FAULT`, `COMMUNICATION_DEGRADED`, and `RECOVERY`.
Sensor faults take precedence over environmental classification; hysteresis
reduces rapid switching near demonstration thresholds.

This design demonstrates autonomy and failure handling. It is not a physical
deployment architecture, radio test, or validated safety controller.
