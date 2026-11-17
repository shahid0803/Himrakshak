# Deployment

HimRakshak has three supported execution modes. Deployment does not turn the
prototype into a field or safety system.

## Local server

The standard-library server binds to loopback by default:

```powershell
python -m himrakshak.server
```

Open `http://127.0.0.1:8765/`. Use `--port 9000` to select another local
port. Local mutation and fault routes are intentionally available for testing.
State and histories are in memory and disappear when the process stops.

## Static dashboard

The GitHub Pages workflow, when present and enabled, publishes only
`site/index.html`. It does not start the Python API, include credentials, or
provide API-backed reliability controls. Direct static use is labelled
`STANDALONE SIMULATION`.

Treat `https://shahid0803.github.io/Himrakshak/` as live only after the Pages
workflow succeeds and the page has been opened. Static mode supports browser
scenarios, local validation, history, and CSV export.

## Render or another remote host

Remote mode is explicit:

```powershell
python -m himrakshak.server --allow-remote
```

The service binds to `0.0.0.0` and uses the platform `PORT` variable (10000 if
unset). Configure:

| Variable | Purpose |
|---|---|
| `PORT` | Platform-assigned listening port |
| `PYTHONUNBUFFERED=1` | Immediate process logs |
| `HIMRAKSHAK_ADMIN_KEY` | Required header for remote mutation routes |
| `HIMRAKSHAK_INGEST_MIN_INTERVAL_SECONDS` | Optional per-station ingest limit |

In remote mode, mutation routes are blocked when no admin key is configured.
Never put the key in the dashboard, repository, or Wokwi sketch. Public
ingestion still validates station identity, payload size, and sequence
ordering. There is no wildcard CORS; the dashboard and API share an origin.

The checked-in `render.yaml` describes the existing Render service. Review its
current deploy record and health response before claiming a particular revision
is deployed. Verify:

```powershell
curl.exe -i https://<service-url>/api/health
curl.exe -i https://<service-url>/
curl.exe -i https://<service-url>/api/stations
```

The health response should report `status: "ok"`, three stations, the
illustrative model identifier, and `illustrative: true`. A health response
proves service availability, not Wokwi execution or model validity.

## Operational limits

Remote state is ephemeral and bounded. Render cold starts can delay the first
request, and restarts reset histories and sequence state. Wake the service
with `/api/health` before a live demonstration, and keep administrative
secrets in the host's secret store.
