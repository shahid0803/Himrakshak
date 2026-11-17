# Simulated communication reliability

Phase 6 adds opt-in, deterministic communication-fault simulation to the
local-only API. These controls model software behavior only: they do not
represent radio performance, network guarantees, or field reliability.

Each station has independent receiver connectivity, counters, and a FIFO
pending queue capped at 10 packets. Dropped packets are validated and counted
but never enter accepted history. Delayed packets are validated and queued;
`release` delivers them in order. A delayed packet counts as one attempted
packet when it enters the queue, not a second attempt when released.

Packets delivered with the same sequence as the latest accepted packet are
reported as `duplicate`. Packets with a lower sequence are reported as
`stale`. Neither outcome changes the latest reading or event history.
Disconnecting a station blocks simulated, normal, and fault-delivery packet
operations while preserving its sequence, random generator, latest reading,
and history. Reconnecting does not reset any of those values.

The additive endpoints are:

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

Packet operations accept a canonical or alias reading directly, or under a
`reading` property. Release, disconnect, and reconnect accept `{}` or an empty
body. Successful responses include an `outcome`; accepted deliveries also
include the normal `reading` and recalculated `assessment`. Communication
counters are returned separately under `communication` and are never added to
raw readings or risk assessments. Invalid packets return structured `400`
errors, unknown stations/actions return `404`, disconnected or full/empty
queue operations return structured conflict errors.

The browser dashboard exposes these controls only when connected to the local
API. Direct standalone mode remains available and explicitly explains that
the reliability endpoints require the local server.
