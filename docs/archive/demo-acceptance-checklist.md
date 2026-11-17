# HimRakshak demo acceptance checklist

Use this checklist after starting a fresh server with
`python -m himrakshak.server --port 8765 --seed 42`. Leave each box unchecked
until the stated evidence is observed.

## Automated checks

- [ ] **Full regression suite** — `python -m unittest discover -s tests -v`
  exits 0 with all tests passing.
- [ ] **High-level acceptance test** —
  `python -m unittest tests.test_demo_acceptance -v` exits 0 and reports the
  real HTTP acceptance path.
- [ ] **Protected hashes** — `himrakshak/schema.py` and
  `docs/data-contract.md` match their documented SHA-256 values.

## HTTP/API checks

- [ ] **Clean startup and health** — `GET /api/health` returns `200`, `ok`,
  three stations, the documented model ID, and `illustrative: true`.
- [ ] **All stations** — `GET /api/stations` lists exactly HR-01, HR-02, and
  HR-03; station detail routes return each station's own state.
- [ ] **Independent station state** — changing or faulting one station does not
  change another station's latest reading, sequence, queue, counters, or
  history.
- [ ] **Normal scenario** — normal simulation returns `NORMAL` and an empty
  reason list.
- [ ] **Warning scenario** — `vibration_burst` returns `WARNING` with the
  documented assessment keys.
- [ ] **Critical scenario** — `heavy_rain` or `slope_shift` returns `CRITICAL`
  with ordered explanation strings.
- [ ] **Dropped packet preservation** — drop returns `outcome: "dropped"` and
  before/after station detail shows unchanged latest, sequence, and history.
- [ ] **Delayed packet release** — delay reports a pending packet; release
  reports delivery and FIFO order; state changes only on release.
- [ ] **Queue-limit rejection** — the 11th pending packet returns structured
  `409` and the queue remains capped at 10.
- [ ] **Duplicate rejection** — a sequence equal to the latest returns
  `outcome: "duplicate"` without appending history.
- [ ] **Stale rejection** — a lower sequence returns `outcome: "stale"` without
  overwriting newer accepted data.
- [ ] **Disconnect/reconnect** — disconnected simulation returns structured
  `409`; reconnect permits a fresh accepted reading without resetting state.
- [ ] **Invalid request preservation** — malformed or incomplete packets return
  structured `400` and do not partially mutate station state.
- [ ] **History bound** — accepted event history does not exceed 100 entries.
- [ ] **Legacy compatibility** — `/api/health`, `/api/state`, `/api/readings`,
  and `/api/simulate` retain their documented response shapes; legacy
  simulation defaults to HR-01.

## Browser checks

- [ ] **API-connected dashboard** — the page reports connected backend mode,
  displays all three stations, and station selection loads station-specific
  state.
- [ ] **Risk display** — normal, warning, and critical controls update the
  visible risk level, score, reasons, and illustrative/demo labels.
- [ ] **Fault controls** — drop, delay, release, duplicate, stale, disconnect,
  and reconnect controls show visible outcome feedback and communication
  counters/pending state.
- [ ] **Standalone `file://` mode** — direct opening loads without an API,
  preserves local scenarios, sliders, import validation, history, and exports,
  and explains that API-backed reliability controls require the local server.
- [ ] **Standalone limitation understood** — no browser-only fault action is
  marked as an API delivery when the server is unavailable.

## Operator cleanup

- [ ] **Clean shutdown** — stop the server with `Ctrl+C` and confirm
  `Get-NetTCPConnection -LocalPort 8765 -State Listen` returns no listener.
- [ ] **Fresh reset** — restart the server and confirm in-memory readings,
  histories, queues, counters, and connection state start empty/connected.

Automated checks are evidence from test output; HTTP checks are evidence from
actual responses and before/after state; browser checks are manual runtime
observations. Do not mark a box from documentation alone.
