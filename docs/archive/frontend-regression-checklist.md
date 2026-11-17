# HimRakshak frontend regression checklist

This checklist records Phase 2 checks against the existing root-level
`index.html`. Browser results below were exercised in the available browser
canvas using the local `file://` URL. Static checks are identified separately.

| Test ID | Preconditions | Action | Expected result | Actual result | Status |
| --- | --- | --- | --- | --- | --- |
| FE-001 | Open root `index.html` locally | Load the dashboard | Initial readings, risk, packet, chart, schematic, controls, and disclosure appear | Page reached browser ready state; initial NORMAL state, packet `0001`, chart, schematic, and prototype disclaimer appeared | PASS |
| FE-002 | Dashboard loaded | Inspect runtime dependencies | Dashboard runs without external runtime assets | Local `file://` page loaded without network/runtime dependency errors observed | PASS |
| FE-003 | Dashboard loaded | Change slider values through input events | Local displayed values and meter state update within ranges without logging every input | Moisture changed to `80` while disconnected; no log was added until the change event | PASS |
| FE-004 | Link connected | Run heavy-rain scenario | Moisture/tilt/vibration, risk, reasons, chart/history, and receiver packet update | Readings became `75 / 4.0 / 28`; risk became CRITICAL; sequence advanced and event was logged | PASS |
| FE-005 | Link connected | Run slope-shift scenario | Tilt scenario updates readings and assessment | Readings became `75 / 11.0 / 55`; risk became CRITICAL; sequence and event log advanced | PASS |
| FE-006 | Link connected | Run vibration-burst scenario | Vibration increases and assessment updates | Vibration became `88`; risk score/reasons and receiver packet updated | PASS |
| FE-007 | Dashboard in a scenario state | Click reset to baseline | Readings return to `32 / 1.2 / 12` and receiver receives the reset packet | Baseline readings and NORMAL state restored; sequence advanced | PASS |
| FE-008 | Link connected | Disconnect simulated link | Link indicates unavailable; receiver packet remains unchanged | Link showed LOST LINK, receiver showed STALE, and the prior packet remained | PASS |
| FE-009 | Link disconnected | Change a slider and release it | Local state changes; no receiver packet is delivered; one event is logged | Moisture changed locally; packet/sequence stayed unchanged and NO LINK event was logged | PASS |
| FE-010 | Link disconnected after local changes | Reconnect simulated link | Latest local state is delivered and sequence advances | Receiver updated to latest moisture, link returned CONNECTED, and sequence advanced | PASS |
| FE-011 | Dashboard loaded | Import valid canonical JSON | Packet imports and browser displays a recalculated assessment | HR-02 canonical packet imported and displayed as WARNING | PASS |
| FE-012 | Dashboard loaded | Import documented legacy aliases | Alias fields are accepted for the browser demonstration | `node`, `moisture`, `tilt`, and `vibration` packet imported successfully | PASS |
| FE-013 | Dashboard loaded | Import malformed JSON | Helpful error appears and valid receiver data is preserved | Parse error feedback appeared; receiver packet and log count were unchanged | PASS |
| FE-014 | Dashboard loaded | Import packet missing a sensor field | Helpful validation error appears and receiver data is preserved | Missing vibration produced an error; prior receiver packet remained | PASS |
| FE-015 | Dashboard loaded | Import boolean sensor value | Boolean is rejected as a numeric reading | After the targeted fix, explicit boolean error appeared and receiver packet remained unchanged | PASS |
| FE-016 | Dashboard loaded | Import out-of-range sensor value | Range error appears and receiver data is preserved | Moisture `101` produced an explicit range error; receiver remained unchanged | PASS |
| FE-017 | Dashboard loaded | Import forged risk label with elevated readings | Incoming risk label is not authoritative | Incoming `NORMAL` was recalculated and displayed as WARNING | PASS |
| FE-018 | Link disconnected | Manually import a virtual packet | Import remains clearly a manual browser operation, not radio delivery | Import succeeded with WOKWI transport text; link remained LOST LINK and no simulated radio packet was claimed | PASS |
| FE-019 | Dashboard loaded | Export a non-empty event log | CSV contains understandable headers and rows | Blob export was intercepted in-browser; header and data rows were generated with quoted CSV cells | PASS |
| FE-020 | Dashboard loaded | Clear log, then export | Empty export behaves sensibly | Log displayed zero events; export contained the header row only | PASS |
| FE-021 | Dashboard loaded | Exercise CSV escaping implementation | Commas, quotes, and newlines are escaped in generated cells | Code inspection confirms `csvCell` doubles quotes and wraps values; no UI-generated special-character event was available | NOT RUN |
| FE-022 | Dashboard loaded | Copy latest receiver packet | Copy succeeds where permitted or gives fallback | Browser blocked copy and displayed the clear manual-copy fallback message | PASS |
| FE-023 | Dashboard loaded | Observe last-seen and stale behavior | Receiver age updates; disconnected state is not shown as current | Last-seen displayed elapsed time and receiver changed to STALE while disconnected | PASS |
| FE-024 | Dashboard loaded | Inspect narrow-screen layout | Controls remain usable at narrow width | Responsive CSS is present; viewport resizing was unavailable in the browser tool | NOT RUN |
| FE-025 | Project root | Run schema suite | Existing Phase 1 tests remain green | `python -m unittest discover -s tests -v`: 22 tests, all passed | PASS |
| FE-026 | Project root | Validate Wokwi diagram and field names | Diagram remains valid and firmware fields match contract | `diagram.json` parsed; 11 parts, 19 connections; all seven canonical fields found in sketch | PASS |
| FE-027 | Project root | Inspect scope | No backend, simulator, API, risk engine, or multi-station implementation is added | No such Phase 3/4 implementation was introduced | PASS |

## Baseline and final identity

The original pre-Phase-2 root dashboard hash was:

```text
49E5F47864F3014902EAE6D37A8841BCF15A93F7342D30B429C92E897002128B
```

The final hash after the targeted boolean-import fix is recorded in the Phase
2 completion report. The only frontend change was the import guard that rejects
boolean values before numeric coercion.

## Testing limitations

- The available browser tool did not provide viewport resizing, so narrow-screen
  runtime behavior remains a manual follow-up.
- Wokwi simulator execution was not performed; only static diagram and field
  compatibility checks were run.
- Clipboard success mode was not available in the local browser context; the
  blocked-permission fallback was verified.
- No new browser automation dependency or test framework was installed.
