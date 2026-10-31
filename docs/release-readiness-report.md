# HimRakshak Phase 8 Release Readiness Report

Date: 2026-10-28

Project: `C:\Users\jk874\OneDrive\Documents\HimRakshak`

## Overall result: INCOMPLETE

The Phase 8 rehearsal passed the automated suite, protected-contract checks,
live local API flow, station isolation, reliability transitions, backend
dashboard import, standalone import, and CSV generation checks. The browser
clipboard API was reached in both modes, but direct operating-system clipboard
reading was denied by the browser canvas because its document was not focused.
Controlled browser doubles verified the application's success and rejection
paths, but they do not prove that the operating-system clipboard received the
text. The project remains INCOMPLETE for that genuine clipboard-verification
gap.

No Phase 9 work was started.

## Baseline and commands

The project is not a Git repository, so no Git diff or commit history is
available. The project directory and files were inspected directly.

Baseline and final automated commands:

```powershell
python -m unittest discover -s tests -v
python -m unittest tests.test_demo_acceptance -v
```

Results:

| Command | Exit status | Result |
|---|---:|---|
| `python -m unittest discover -s tests -v` | 0 | 95 passed, 0 failed, 0 skipped |
| `python -m unittest tests.test_demo_acceptance -v` | 0 | 1 passed, 0 failed, 0 skipped |

No warnings or intermittent failures were observed. The acceptance test
started and stopped its own ephemeral server successfully.

## Protected and release hashes

| File | Expected SHA-256 | Actual SHA-256 | Status |
|---|---|---|---|
| `himrakshak/schema.py` | `C2CC32C8649064EF7630486BA45A83FB542CE3927FD48A489B59C66CC07C6A92` | `C2CC32C8649064EF7630486BA45A83FB542CE3927FD48A489B59C66CC07C6A92` | PASS |
| `docs/data-contract.md` | `2D17FB685001FB6FED0008880E6292F8492534722B2F1461E9A90B0B4A6E18DF` | `2D17FB685001FB6FED0008880E6292F8492534722B2F1461E9A90B0B4A6E18DF` | PASS |

Current release hashes:

| File | SHA-256 |
|---|---|
| `index.html` | `360048D5803FCFAA1DC7B69FAE253E0A9AC511409C5FAE1B448CD96EC953D108` |
| `himrakshak/reliability.py` | `8E3237D6E11C1E3DD1485CF9740C8938F7F62DCD0C640E7565B24162CED5E4CF` |
| `himrakshak/server.py` | `D8E1C4346EF27F0CC97871539BFCB9A793C2CB2975222923EBF6FDE01C9926DB` |
| `himrakshak/stations.py` | `A1841D84B75A39F42AF309D04F84E583966CB49F1C3B3F47D593CC2C525A4062` |
| `README.md` | `984C5D02DE5220C41B1C479AD44724F42330AF49F91122AA551E6B2B6EB7E581` |
| `docs/demo-runbook.md` | `DB4266E3A2C0B1EAA346739ECC7A819764FB3FC0B294FFF1F5F2141359BE8468` |
| `docs/demo-acceptance-checklist.md` | `EEF197540D468FA9A6173D94867FC09691AE801917C78783369FA1BF31F8D63A` |

## End-to-end local API rehearsal

The documented command was used:

```powershell
python -m himrakshak.server --port 8765 --seed 42
```

Observed results:

| Check | Result |
|---|---|
| `GET /api/health` | HTTP 200; status `ok`; three stations; model `himrakshak-demo-risk-engine-v1`; illustrative `true` |
| `GET /api/stations` | HR-01, HR-02, HR-03 returned |
| Normal simulation | `NORMAL` |
| Drop fault | `dropped`; latest, next sequence, and event count unchanged |
| Delay fault | `pending`; pending queue reported 1 |
| Release | `delivered` |
| Duplicate fault | `duplicate` |
| Disconnect | `disconnected` |
| Simulation while disconnected | HTTP 409 |
| Reconnect | `reconnected` |
| Fresh post-reconnect simulation | accepted with sequence 0 for untouched HR-02 |

The API rehearsal confirmed that rejected communication operations did not
overwrite accepted state and that station-specific state remained isolated.

## Dashboard browser rehearsal

### Backend mode

URL: `http://127.0.0.1:8765/`

Completed:

- Connected backend mode was detected and displayed.
- Three-station roster and station selection were visible.
- HR-01 imported a valid packet through the Python API and displayed
  `NORMAL`, sequence `0002`, and accepted feedback.
- An invalid boolean sensor packet was rejected by the API; the displayed
  packet remained unchanged.
- Scenario control for HR-02 produced a `WARNING` result after station
  selection.
- Reliability counters and delayed-packet history were displayed.
- CSV export was triggered; intercepted generated content had the documented
  header and three data rows.
- Copy control was triggered; the browser displayed:
  `Copy was blocked by this browser. Select the JSON text and copy it manually.`

Not independently verified:

- The actual contents of the system clipboard.
- Native browser download persistence outside the intercepted Blob-generation
  check.

### Direct `file://` standalone mode

URL:
`file:///C:/Users/jk874/OneDrive/Documents/HimRakshak/index.html`

Completed:

- Standalone mode loaded without the API.
- A valid Wokwi-style JSON packet was imported and reassessed locally as
  `CRITICAL`; forged incoming risk metadata was not trusted.
- Malformed JSON, non-object JSON, boolean sensor values, and conflicting
  canonical/alias fields produced visible error feedback.
- Invalid imports preserved the previously accepted packet.
- CSV export was triggered; generated content contained the documented header
  and two data rows.
- Copy control was triggered and reported browser success through the
  available fallback path.

Not independently verified:

- The actual system clipboard contents.
- Native download-file persistence.
- API-backed reliability controls in standalone mode; the UI correctly
  identifies these as backend-only.

### Clipboard verification details

Real browser API attempt:

| Page | Clipboard API | `readText()` result | UI result |
|---|---|---|---|
| `http://127.0.0.1:8765/` | Available in secure context | `NotAllowedError: Document is not focused` | Blocked-copy error shown |
| `file://.../index.html` | Available in secure context | `NotAllowedError: Document is not focused` | Blocked-copy error shown |

Controlled browser-level doubles executed the actual dashboard handler:

- Success double captured a non-empty string exactly equal to
  `#packetJson.textContent`, displayed `Latest receiver packet copied as JSON.`,
  and left packet/history state unchanged.
- Rejection double made `writeText()` reject with `NotAllowedError`, displayed
  `Copy was blocked by this browser. Select the JSON text and copy it manually.`,
  and left packet/history state unchanged.

These are mocked/API-level browser checks, not genuine operating-system
clipboard verification. No Node or third-party browser framework was installed;
the existing browser automation was sufficient for the controlled doubles.

## Reliability, isolation, and compatibility evidence

Automated tests and live requests covered:

- Dropped packets preserving accepted state.
- FIFO delayed release and queue capacity of 10.
- Duplicate and stale sequence rejection.
- Disconnect blocking delivery with HTTP 409 behavior.
- Reconnection accepting fresh packets without resetting the station.
- Independent station sequences, histories, counters, and connection state.
- Invalid request preservation.
- Legacy `/api/health`, `/api/state`, `/api/readings`, and `/api/simulate`
  compatibility.
- Existing risk result shape, reason ordering, model identifier, thresholds,
  and illustrative flag.

No unresolved API, reliability, schema, risk-model, or station-isolation
failure was observed.

## Files changed in Phase 8

Only this report was created:

- `docs/release-readiness-report.md` — records the actual Phase 8 rehearsal
  evidence and remaining clipboard limitation.

No source, protected contract, test, dashboard, or simulator files were
modified during the rehearsal.

## Immediate pre-demo steps

1. Start the server with `python -m himrakshak.server --port 8765 --seed 42`.
2. Open `http://127.0.0.1:8765/` and confirm the connected-backend indicator.
3. Run the normal, warning, critical, and one reliability-fault walkthrough
   from `docs/demo-runbook.md`.
4. Stop the server and confirm port 8765 is released.

## Final limitation

Operating-system clipboard contents remain **NOT VERIFIED** by this
environment. The application-level success and denial behavior is verified,
but the browser canvas denies direct clipboard reads when its document is not
focused. This is the only release-readiness item preventing a PASS
classification.
