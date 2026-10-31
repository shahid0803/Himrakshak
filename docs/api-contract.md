# HimRakshak local API contract

Phase 5 adds a standard-library HTTP server for three independent virtual
nodes. It binds to
`127.0.0.1` only and serves the existing dashboard and JSON API from the same
origin:

```powershell
python -m himrakshak.server --port 8765 --seed 42
```

Open `http://127.0.0.1:8765/`. No package installation or internet connection
is required. Opening `index.html` directly keeps the existing standalone
browser simulation. When served by the local server, the dashboard probes `GET /api/health`,
loads the station roster, and uses the Python API as the authoritative
assessment source. If that probe fails, it remains in standalone mode without
repeated requests.

## Endpoints

### `GET /api/health`

Returns:

```json
{
  "status": "ok",
  "service": "HimRakshak",
  "api_version": 1,
  "model_id": "himrakshak-demo-risk-engine-v1",
  "illustrative": true,
  "station_count": 3
}
```

### `GET /api/state`

Returns the latest accepted result and at most 100 in-memory events:

```json
{
  "latest": {"reading": {}, "assessment": {}},
  "events": [{"reading": {}, "assessment": {}}]
}
```

`latest` is `null` before the first accepted reading.

### `GET /api/stations`

Returns the fixed roster and the bounded global event history:

```json
{
  "stations": [
    {
      "node_id": "HR-01",
      "label": "Virtual Station 1",
      "scenario": "normal",
      "receiver_connected": true,
      "next_sequence": 0,
      "latest": null,
      "event_count": 0
    }
  ],
  "events": []
}
```

The response contains all three stations. Global events are capped at 100 and
include `station_id` and `station_label`.

### `GET /api/stations/{node_id}`

Returns `{"station": ...}` with the station summary, latest result, and that
station's bounded event history. Unknown station IDs return structured `404`.

### `POST /api/readings`

Accepts one JSON object containing canonical fields or the documented aliases
`node`, `moisture`, `tilt`, and `vibration`. The server validates and
normalizes it with `validate_reading()`, recalculates risk with
`assess_risk()`, stores the accepted result, and returns:

```json
{"reading": {}, "assessment": {}}
```

Incoming risk fields are ignored. Rejected packets do not replace `latest` or
add an event.

### `POST /api/simulate`

Accepts one object such as `{"scenario":"heavy_rain"}`. Supported scenarios
are `normal`, `heavy_rain`, `slope_shift`, and `vibration_burst`. The existing
`SensorSimulator` produces a schema-normalized packet and the server returns
and stores the recalculated reading/assessment result. Sequence values
increase across scenario changes for the single virtual node.

### `POST /api/stations/{node_id}/simulate`

Accepts `{"scenario":"normal"}` and generates the next packet for the
specified station. Its simulator, scenario, sequence, and history are
independent of the other two stations.

### `POST /api/stations/{node_id}/readings`

Accepts a canonical or alias packet whose normalized `node_id` matches the
URL. The server assigns the station's next sequence number before the final
validation and assessment. Identity mismatches are rejected with `400`.

### Simulated reliability endpoints

Phase 6 adds opt-in software-only communication fault controls:

```text
POST /api/stations/{node_id}/faults/drop
POST /api/stations/{node_id}/faults/delay
POST /api/stations/{node_id}/faults/release
POST /api/stations/{node_id}/faults/deliver
POST /api/stations/{node_id}/faults/duplicate
POST /api/stations/{node_id}/faults/stale
POST /api/stations/{node_id}/faults/disconnect
POST /api/stations/{node_id}/faults/reconnect
```

Drop and delay accept a canonical or alias packet directly or as
`{"reading": {...}}`. Release, disconnect, and reconnect accept `{}`. Delayed
packets use a station-local FIFO queue capped at 10 and are not added to
accepted state until released. Responses contain an `outcome` and a separate
`communication` object with connection state, pending count, and bounded
counters. Duplicate and stale outcomes do not replace the latest packet or
append history. These are deterministic communication simulations, not
physical radio tests.

The legacy routes remain backward-compatible and default to HR-01. A
recognized `station` or `node_id` may select another station in the legacy
simulation/reading request.

## Errors and limits

Malformed JSON, non-object bodies, invalid packets, alias conflicts, and
unknown scenarios return HTTP `400`. Unknown routes return `404`; unsupported
methods on known routes return `405`; bodies over 64 KiB return `413`.
Errors use:

```json
{"error":{"status":400,"message":"human-readable message"}}
```

The service uses a bounded in-memory history of 100 accepted events and has no
database or persistence. State is lost when the process stops. The server is
local-only and has no authentication; do not bind it to a public interface.
This remains a software demonstration with synthetic readings and illustrative
thresholds, not a validated landslide prediction or field safety system.
