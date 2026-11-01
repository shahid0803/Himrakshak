# HimRakshak deployment documentation

This document describes deployment procedures for HimRakshak across static hosting, local execution, and the existing Render service.

---

## 1. Static demo deployment (GitHub Pages)

The public static HimRakshak demo is published as a GitHub Pages static site. The site contains only the self-contained `index.html` dashboard; Python source, tests, documentation, Wokwi files, caches, and local snapshots remain in the source repository but are not copied into the Pages artifact.

### Deployment workflow

`.github/workflows/pages.yml` runs on pushes to `main` and on manual workflow dispatch. It:

1. Checks out the repository.
2. Configures GitHub Pages.
3. Creates a clean `site/` directory containing only `index.html`.
4. Verifies that the entry page exists and that no extra top-level artifact files were created.
5. Uploads the directory with `actions/upload-pages-artifact`.
6. Deploys it through the `github-pages` environment with `actions/deploy-pages`.

The workflow does not start the Python API and does not publish credentials, development archives, tests, or caches.

### Expected public URL

`https://shahid0803.github.io/Himrakshak/`

Treat the URL as usable only after the Pages workflow completes successfully and the page has been opened and checked. A workflow file or repository setting alone is not evidence that the site is live.

### Online functionality

The hosted static page runs the browser-only simulation. Visitors can:

- Select the three virtual stations (HR-01, HR-02, HR-03).
- Run normal, heavy-rain, slope-shift, and vibration-burst scenarios.
- Adjust simulated sensor controls.
- Import a virtual JSON packet for local validation and reassessment.
- View local history and export the browser session as CSV.

The page visibly identifies itself as a **Static hosted demo · Python API unavailable** when the local API probe cannot connect. It does not claim that an API request, packet delivery, or reliability operation succeeded.

---

## 2. Local development server (Mode A)

The Python backend is designed to run locally with zero external dependencies, standard-library only, and without an internet connection.

### Local startup instructions

```powershell
python -m himrakshak.server
```

- **Default Host:** `127.0.0.1` (loopback only)
- **Default Port:** `8765`
- **Dashboard & API URL:** `http://127.0.0.1:8765/`

### Custom local port override

```powershell
python -m himrakshak.server --port 9000
```

- **Bound Address:** `127.0.0.1:9000`

### Local security and testing behavior

1. **Loopback Protection:** The local server binds strictly to the loopback interface (`127.0.0.1`). Any attempt to pass `--host 0.0.0.0` without the explicit `--allow-remote` flag is rejected with an error.
2. **Unauthenticated Local Testing:** Mutation endpoints (`/api/simulate`, `/api/stations/{node_id}/simulate`, and `/api/stations/{node_id}/faults/*`) are open for local interactive development and test automation without requiring an administrative key.
3. **No Artificial Rate Limiting:** Local ingestion runs with rate limiting disabled by default (`interval = 0.0s`), ensuring tests and local simulations execute with maximum speed.

---

## 3. Cloud-ready server (Mode B)

Phase 11 verifies the explicit cloud execution mode for hosting platforms such as Render, Railway, or containerized environments.

### Cloud startup command

```powershell
python -m himrakshak.server --allow-remote
```

- **Bound Host:** `0.0.0.0` (all interfaces)
- **Bound Port:** Dynamically resolved from the platform-injected `PORT` environment variable (e.g. `10000` on Render). If `PORT` is omitted in remote mode, it defaults to `10000`.

### Required and optional environment variables

| Variable | Required? | Default | Purpose |
|:---|:---:|:---:|:---|
| `PORT` | Set by cloud host | `10000` (in remote mode) | TCP port allocated by the cloud container router. |
| `PYTHONUNBUFFERED` | Recommended | `1` | Ensures real-time standard output and error logging. |
| `HIMRAKSHAK_ADMIN_KEY` | Recommended | `None` | Shared administrative secret. When set, all `/simulate` and `/faults/*` mutation routes require header `X-HimRakshak-Admin: <key>`. In remote mode, if this variable is unset, mutation routes are completely disabled (HTTP 403) for security. |
| `HIMRAKSHAK_INGEST_MIN_INTERVAL_SECONDS` | Optional | `3.0` (in remote mode) | Minimum elapsed seconds between accepted readings per station. Prevents resource exhaustion from rapid sender loops. Exceeded requests return HTTP 429 with `Retry-After`. |

---

## 4. Render deployment

The repository includes the `render.yaml` Blueprint for the existing Render Web
Service at https://himrakshak.onrender.com/. A successful health response proves
the service is live, but does not prove which GitHub commit Render deployed.
Check the Render dashboard's deploy record before describing a particular commit
or firmware revision as deployed.

### Deploy with the Render Blueprint

1. Sign in to Render and choose **New → Blueprint** for a new environment or service.
2. Connect the GitHub repository `shahid0803/Himrakshak` and select the `main` branch.
3. Review the `himrakshak` web service shown by `render.yaml`.
4. Keep the **Free** plan selected; do not add a paid database or other paid resource.
5. Apply the Blueprint. Render generates `HIMRAKSHAK_ADMIN_KEY` as a secret environment value.
6. Wait for the deploy to finish, then open the service URL shown in the Render dashboard.
7. Keep the generated admin key in Render only. Never copy it into `index.html`, browser developer tools, GitHub, or a public Wokwi sketch.

### Web service parameters

- **Service Type:** Web Service
- **Runtime:** `Python` (Python 3.10–3.14 compatible)
- **Plan:** `Free` ($0/month, 750 free instance hours per month)
- **Build Command:** `pip --version` (or leave empty; no external packages needed)
- **Start Command:** `python -m himrakshak.server --allow-remote`
- **Health Check Path:** `/api/health`
- **Auto-Deploy:** Yes (on push to `main`)

### Health-check verification

Render continuously monitors the health-check path:

```text
GET /api/health
```

Expected HTTP 200 JSON payload:

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

The health check does not modify station state, requires no authentication, and executes in sub-millisecond time.

### Post-deployment endpoint checklist

Replace `<service-url>` with the HTTPS URL displayed by Render:

```powershell
curl.exe -i https://<service-url>/api/health
curl.exe -i https://<service-url>/api/state
curl.exe -i https://<service-url>/api/stations
curl.exe -i https://<service-url>/api/stations/HR-01
curl.exe -i https://<service-url>/api/stations/HR-02
curl.exe -i https://<service-url>/api/stations/HR-03
curl.exe -i https://<service-url>/
curl.exe -i -X POST https://<service-url>/api/stations/HR-01/simulate `
  -H "Content-Type: application/json" -d '{"scenario":"normal"}'
```

Confirm that:

- `/api/health` returns HTTP 200 and `"status": "ok"`.
- `/api/state` responds with the current state payload.
- `/api/stations` returns exactly `HR-01`, `HR-02`, and `HR-03`.
- Each station endpoint returns the corresponding station details and history.
- `/` serves the dashboard over HTTPS and its indicator identifies the connected backend.
- The unauthenticated mutation request returns HTTP 401 or 403 without changing station state.
- The dashboard displays a clear message that remote administrative controls are unavailable when a protected mutation is rejected.
- Local mode still starts with `python -m himrakshak.server` and remains bound to `127.0.0.1`.

### Cloud security controls

1. **Station Allowlisting:** Public ingestion accepts readings only for registered stations: `HR-01`, `HR-02`, and `HR-03`. Unrecognized stations return HTTP 404.
2. **Sequence Conflict Detection:** Incoming device packets are sequence-validated. Duplicate and out-of-order stale sequence numbers are rejected with HTTP 409 Conflict without altering the station's latest accepted reading.
3. **Payload Limit:** Request bodies larger than 64 KiB are rejected with HTTP 413.
4. **Administrative Protection:** Simulated fault-injection endpoints cannot be tampered with by public internet users. When running with `--allow-remote`, mutations are blocked unless authorized with `X-HimRakshak-Admin`.
5. **No Wildcard CORS:** The deployment follows a unified single-service model where the dashboard (`/`) and API (`/api/`) share the same origin, eliminating cross-origin exposure.

### Dashboard synchronization

The dashboard determines its mode from the page origin and a successful
same-origin `/api/health` response:

- A `file:` URL is **STANDALONE SIMULATION** and uses browser-only state.
- A loopback URL with a working API is **LOCAL BACKEND CONNECTED**.
- Any other host with a working API is **CLOUD BACKEND CONNECTED**.
- Failed probes are shown as **LOCAL BACKEND UNAVAILABLE** or
  **CLOUD BACKEND UNAVAILABLE** and retain the last server values without
  generating replacement cloud readings.

In connected modes, `/api/state`, `/api/stations`, and the selected
`/api/stations/{node_id}` response hydrate the dashboard. The page polls these
same-origin endpoints every five seconds, prevents overlapping refreshes,
pauses polling while hidden, and refreshes on visibility recovery. Public
reading ingestion remains available for sensor sliders and Wokwi packet import;
remote scenario and fault controls remain disabled because they require the
protected administrative header and the browser never receives the admin key.

---

## 5. Cold-start and ephemeral-memory limitations

Render's free compute tier spins down web services after **15 minutes of inactivity**:

1. **Cold Start Delay:** When a visitor or Wokwi simulator reaches a sleeping Render service, the container takes approximately **50–60 seconds** to boot. Subsequent requests respond immediately.
2. **Ephemeral In-Memory State:** Station history (capped at 100 events per station) and sequence numbers are stored in process RAM. When Render restarts or scales down the container, all history resets to the initial baseline.
3. **Professor Demonstration Runbook:** To ensure a smooth live presentation, the presenter should send a request to `GET /api/health` approximately 2 minutes before the demonstration to wake up the service.

---

## 6. How to stop cloud service or disable remote mode

- **Disable Remote Access Locally:** Omit the `--allow-remote` flag when launching the server. It will bind exclusively to `127.0.0.1:8765`.
- **Suspend Render Service:** In the Render Dashboard, navigate to the web service settings and select **Suspend Service**.
- **Regenerate / Revoke Admin Key:** In the Render Dashboard under **Environment**, change or delete `HIMRAKSHAK_ADMIN_KEY`.
