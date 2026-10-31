# HimRakshak sensor-data contract

This document defines the Phase 1 canonical input contract for future sensor
simulators, the risk engine, the local API, dashboard integration, and the
Wokwi demonstration.

The ranges below are software-prototype validation ranges. They are not
certified geological limits, equipment limits, or safety thresholds.

## Required fields

| Field | Type | Validation |
| --- | --- | --- |
| `node_id` | string | Trimmed value must be non-empty and at most 40 characters |
| `sequence` | integer | Non-negative integer; booleans are not accepted |
| `timestamp` | string | Timezone-aware ISO 8601 timestamp for normal telemetry |
| `soil_moisture_pct` | number | Inclusive range 0 to 100 |
| `tilt_deg` | number | Inclusive range 0 to 15 for this demonstration |
| `vibration_level` | number | Inclusive range 0 to 100 |
| `battery_pct` | number | Inclusive range 0 to 100 |

Booleans are rejected for all numeric fields. Numeric values must be finite;
NaN and positive or negative infinity are invalid. Missing fields are errors,
not implicit safe or zero readings.

## Canonical example

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

UTC, expressed with `Z`, is the preferred timestamp format for generated
telemetry. Other valid ISO 8601 timezone offsets are accepted.

## Legacy field aliases

For compatibility with the original dashboard packet format, these aliases
may substitute for a missing canonical field:

| Legacy alias | Canonical field |
| --- | --- |
| `node` | `node_id` |
| `moisture` | `soil_moisture_pct` |
| `tilt` | `tilt_deg` |
| `vibration` | `vibration_level` |

The alias value is validated with the same type, finite-number, and range
rules as its canonical field. A packet containing both forms is accepted only
when the values match; conflicting canonical and alias values are rejected.
The normalized result contains the canonical field names and removes these
legacy alias keys. Unrelated metadata remains preserved.

## Wokwi exception

The current Wokwi sketch uses `"timestamp": "SIMULATED"` because it does not
have a wall-clock source. That marker is accepted only when
`transport` is exactly `"WOKWI_SOFTWARE_SIMULATION"`. It must not be used to
claim a real-world date or to calculate real-world freshness.

## Optional metadata

`transport` may identify the source, for example:

- `SIMULATED_LORA`
- `WOKWI_SOFTWARE_SIMULATION`

Unknown metadata is retained by the standard-library validator so callers can
log or inspect it. Derived fields such as `risk`, `risk_score_demo`, and
`reasons` are not sensor measurements and are never trusted as authoritative
risk results. A future risk engine must recalculate its own assessment.

## Validator

Use `himrakshak.schema.validate_reading(payload)`. It returns a normalized
dictionary or raises `SchemaValidationError` with the invalid field named in
the message. The validator only checks and normalizes readings; it does not
generate readings or calculate risk.
