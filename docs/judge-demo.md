# Judge demonstration guide

## Start the software demonstration

From the repository root:

```powershell
python -m himrakshak.server --port 8765 --seed 42
```

Open `http://127.0.0.1:8765/`, select HR-01, HR-02, or HR-03, and use the V2
scenario controls:

| Scenario | Expected TinyML class | Expected final state |
|---|---:|---|
| Normal | 0 | NORMAL |
| Heavy rain | 1 | WARNING |
| Slope shift | 2 | CRITICAL |
| Vibration burst | 1 | WARNING |

These are deterministic synthetic backend scenarios, not remotely controlled
Wokwi inputs. The backend recomputes inference and conservative rule/model
fusion.

## Suggested presentation

1. Show the architecture and identify virtual versus derived inputs.
2. Run Normal, Heavy rain, Slope shift, and Vibration burst.
3. Switch stations and show independent state.
4. Open `/api/v2/features` and `/api/v2/model` to show contracts and model
   provenance.
5. Run the [Wokwi procedure](wokwi-guide.md) and show local indicators.
6. Disconnect simulated communication, show local operation and queueing, then
   reconnect and show recovery.
7. State that metrics are synthetic-label agreement and the prototype is not a
   life-safety device.

## Useful checks

```powershell
Invoke-RestMethod http://127.0.0.1:8765/api/health
Invoke-RestMethod http://127.0.0.1:8765/api/v2/features
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:8765/api/v2/demo `
  -ContentType 'application/json' `
  -Body '{"station_id":"HR-01","scenario":"slope_shift"}'
```

Do not present a live cloud deployment, Wokwi project, physical instrument, or
real-world ML result unless it has been independently verified at
demonstration time.
