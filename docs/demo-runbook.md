# HimRakshak demo runbook

HimRakshak is a local, software-only demonstration. Sensor values, packet
delivery, risk scores, and explanations are simulated. They are not physical
measurements, radio guarantees, or a validated landslide-prediction service.

## Prerequisites and startup

The project uses Python's standard library only. No package installation,
cloud account, database, hardware, or internet connection is required.

Open Windows PowerShell and change to the project directory:

```powershell
Set-Location 'C:\Users\jk874\OneDrive\Documents\HimRakshak'
```

Run the local API with the reproducible seed used by the tests:

```powershell
python -m himrakshak.server --port 8765 --seed 42
```

The service binds to `127.0.0.1` and the dashboard is available at
`http://127.0.0.1:8765/`. In another PowerShell window, check health:

```powershell
Invoke-RestMethod http://127.0.0.1:8765/api/health
```

The response should contain `status: "ok"`, `api_version: 1`,
`station_count: 3`, model ID `himrakshak-demo-risk-engine-v1`, and
`illustrative: true`.

If port 8765 is occupied, stop the process using it or select another local
port, for example `--port 8876`, and use that port in the health URL and
dashboard URL. If startup fails, confirm that PowerShell is in the project
directory and that Python is available with `python --version`. If the health
check fails, the dashboard can still be opened directly as
`file:///C:/Users/jk874/OneDrive/Documents/HimRakshak/index.html`; it will use
standalone browser simulation. API-backed reliability controls require the
local server.

## Opening demonstration

1. Open `http://127.0.0.1:8765/`. The page should say
   `Connected backend mode · local Python API`.
2. Confirm the roster contains `HR-01`, `HR-02`, and `HR-03`. Select each
   station and observe its own summary, latest reading, and event count.
3. With HR-01 selected, click **Reset to baseline** or use:

   ```powershell
   Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8765/api/stations/HR-01/simulate `
     -ContentType 'application/json' -Body '{"scenario":"normal"}'
   ```

   The result contains a normalized `reading` and an `assessment` with exactly
   `risk_level`, `demo_score`, `reasons`, `model_id`, and `illustrative`.
   Normal conditions are expected to be `NORMAL` with an empty `reasons` list.
4. Click **Vibration burst**. The supported scenario is expected to produce a
   `WARNING` demonstration result. Read the score, reasons, and the
   `illustrative` label.
5. Click **Simulate heavy rain** or **Simulate slope shift**. These are
   expected to produce `CRITICAL` demonstration results. The reasons are
   generated from the sensor values in a fixed order; they are explanations of
   the demo rules, not predictions.
6. To exercise a second station, select HR-02 and run **Simulate heavy rain**.
   Return to HR-01 and confirm its latest reading and history were not replaced
   by HR-02's state.

## Communication-reliability demonstration

The dashboard's **Simulated communication faults** controls operate against
the selected station when the API is connected. The API operations below make
the packet and state transitions explicit. Use a valid packet such as:

```powershell
$packet = @{
  node_id = 'HR-01'
  sequence = 1
  timestamp = '2026-10-28T03:30:00Z'
  soil_moisture_pct = 32.4
  tilt_deg = 0.9
  vibration_level = 11.1
  battery_pct = 82.0
  transport = 'PYTHON_SIMULATOR'
} | ConvertTo-Json -Compress
```

1. **Drop:** after HR-01 has an accepted reading, call
   `POST /api/stations/HR-01/faults/drop` with `$packet`. The response has
   `outcome: "dropped"`. Refresh
   `GET /api/stations/HR-01`; `latest`, `next_sequence`, and `event_count`
   remain unchanged.
2. **Delay and release:** call
   `POST /api/stations/HR-01/faults/delay` with `$packet`. The response has
   `outcome: "pending"` and the station detail reports
   `communication.pending_packets: 1`. Call
   `POST /api/stations/HR-01/faults/release` with `{}`. The response has
   `outcome: "delivered"` and a `released_order`; the packet appears in
   `latest` and history only after release. Multiple delayed packets release
   FIFO.
3. **Duplicate:** submit a packet whose `sequence` equals the latest accepted
   sequence to `POST /api/stations/HR-01/faults/duplicate`. The response has
   `outcome: "duplicate"` and accepted state does not change.
4. **Stale/out-of-order:** submit a packet with a lower sequence to
   `POST /api/stations/HR-01/faults/stale`. The response has `outcome: "stale"`;
   the newer latest reading and history remain intact.
5. **Disconnect:** click **Disconnect**, or call
   `POST /api/stations/HR-01/faults/disconnect` with `{}`. A subsequent
   simulation returns a structured `409` error and is not counted as delivered.
   The station detail shows `connection_state: "disconnected"`.
6. **Reconnect:** click **Reconnect**, or call
   `POST /api/stations/HR-01/faults/reconnect` with `{}`. A fresh valid
   simulation is accepted without resetting that station's sequence or history.
7. **Queue limit:** delay 10 valid packets for one station. The 11th delay
   returns structured `409` and the pending count remains 10.
8. **Isolation:** select another station or use its station-specific route.
   Its connection state, counters, pending queue, sequence, latest reading,
   and history remain independent.

PowerShell examples for an empty-body operation:

```powershell
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8765/api/stations/HR-01/faults/release `
  -ContentType 'application/json' -Body '{}'
```

## Clean shutdown and reset

Stop the foreground server with `Ctrl+C`. If it was started in another
PowerShell process, stop that specific process and confirm the port is free:

```powershell
Get-NetTCPConnection -LocalPort 8765 -State Listen -ErrorAction SilentlyContinue
```

No output means no process is listening on that port. Restarting
`python -m himrakshak.server --port 8765 --seed 42` creates a fresh in-memory
state: accepted readings, histories, pending queues, counters, connection
states, and simulator progress are lost. The seed makes generated sensor
values reproducible from that fresh start.

If the dashboard says standalone mode, verify the health URL and that the
browser is using the same port. If the standalone page is opened directly,
API-only reliability controls explain that the local server is required;
local scenarios, sliders, import validation, receiver simulation, history,
and exports remain available.

## Repeatable checks

Run the complete regression suite:

```powershell
python -m unittest discover -s tests -v
```

Run the high-level acceptance smoke test:

```powershell
python -m unittest tests.test_demo_acceptance -v
```

The smoke test starts an ephemeral local server, exercises real HTTP
request/response paths, checks state transitions and station isolation, and
stops its server before exiting.
