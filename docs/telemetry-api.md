# Telemetry and API contracts

This is the canonical contract for firmware, simulators, backend, and
dashboard integration.

## Canonical reading

Required fields are `node_id`, non-negative integer `sequence`, timezone-aware
ISO 8601 `timestamp`, `soil_moisture_pct` (0-100), `tilt_deg` (0-15),
`vibration_level` (0-100), and `battery_pct` (0-100). Numeric booleans,
non-finite values, missing fields, and invalid objects are rejected.

Legacy aliases `node`, `moisture`, `tilt`, and `vibration` are accepted only
when they do not conflict with their canonical forms. Use
`himrakshak.schema.validate_reading(payload)` for normalization.

Wokwi may use `"timestamp": "SIMULATED"` only with
`transport: "WOKWI_SOFTWARE_SIMULATION"`. Derived risk fields are never trusted
as measurements or authoritative assessment.

Example:

```json
{
  "node_id": "HR-01",
  "sequence": 1,
  "timestamp": "2026-10-27T12:30:00Z",
  "soil_moisture_pct": 68,
  "tilt_deg": 4.2,
  "vibration_level": 35,
  "battery_pct": 82
}
```

The sender owns `sequence`. The backend preserves accepted device sequences
and separately tracks server event order and simulator sequence generation.

## HTTP endpoints

- `GET /api/health`, `/api/state`, `/api/stations`,
  `/api/stations/{node_id}`
- `POST /api/readings`, `/api/simulate`,
  `/api/stations/{node_id}/readings`,
  `/api/stations/{node_id}/simulate`
- `GET /api/v2/features`, `/api/v2/model`
- `POST /api/v2/demo` with `station_id` and one of `normal`, `heavy_rain`,
  `slope_shift`, or `vibration_burst`
- `GET /api/intelligence/registry`, `/api/intelligence/observations`,
  `/api/intelligence/models`

Accepted readings return `reading` and recalculated `assessment`. Incoming
risk labels are ignored. Duplicate or stale sequences return HTTP 409 with
classification and sequence details. Malformed requests return structured
400 errors; unknown routes return 404; oversized bodies return 413.

V2 model features are ordered as rainfall intensity, rainfall 1-hour,
rainfall 24-hour, surface moisture, subsurface moisture, pore pressure,
tilt-X, tilt-Y, tilt rate, ground displacement, displacement rate, vibration
RMS, vibration peak, dominant frequency, soil temperature, and groundwater
depth. The feature endpoint is authoritative for the serialized contract.

## Reliability controls

Local-only fault routes are:

```text
/api/stations/{id}/faults/drop
/api/stations/{id}/faults/delay
/api/stations/{id}/faults/release
/api/stations/{id}/faults/deliver
/api/stations/{id}/faults/duplicate
/api/stations/{id}/faults/stale
/api/stations/{id}/faults/disconnect
/api/stations/{id}/faults/reconnect
```

Each station has a FIFO queue capped at 10 and a bounded history capped at
100. Fault simulation models software behavior only; it is not radio or field
reliability evidence. Local mutation routes are open for local testing.
Remote mutation routes require the deployment admin key or are disabled.

The service binds to loopback by default and has no persistence unless
explicitly configured for the separate intelligence store. See
[deployment](deployment.md) for remote operation.
