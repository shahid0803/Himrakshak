# HimRakshak virtual stations

Phase 5 provides exactly three independent in-memory virtual stations:

| Node | Label |
|---|---|
| `HR-01` | Virtual Station 1 |
| `HR-02` | Virtual Station 2 |
| `HR-03` | Virtual Station 3 |

Each station owns its simulator, scenario, sequence counter, latest accepted
reading, receiver/link state, and bounded event history. The manager also keeps
a bounded global history of the latest 100 accepted events; every event
identifies its originating station. A rejected packet never changes station
state or either history.

When a base seed is supplied to the server, station seeds are derived
deterministically as `base_seed + roster_index`: HR-01 uses the base seed,
HR-02 uses base plus one, and HR-03 uses base plus two. This avoids Python's
process-randomized `hash()` and makes repeated server runs reproducible.
Changing a station scenario does not reset its simulator or sequence.

Station-specific readings must contain a `node_id` matching the URL. The
server assigns the next monotonic sequence for that station, validates the
normalized packet, and recalculates the assessment. Legacy Phase 4 routes
remain available and default to HR-01; `/api/readings` and `/api/simulate` may
also select a recognized station explicitly.

The dashboard selector switches the active station without carrying readings,
history, scenario state, or receiver state from another station. Direct
`file://` use keeps the same three station records locally; served use loads
the corresponding station endpoint from the local Python API.
