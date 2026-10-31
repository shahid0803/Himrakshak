# HimRakshak demonstration risk model

The Phase 3 Python engine mirrors the existing browser demonstration rules. It
is a transparent software prototype, not a calibrated probability model or a
validated landslide-prediction system.

## Score

For a validated reading:

```text
moisture_part = max(0, (soil_moisture_pct - 45) / 55) * 35
tilt_part = max(0, (tilt_deg - 2) / 10) * 45
vibration_part = max(0, (vibration_level - 25) / 75) * 20
raw_score = moisture_part + tilt_part + vibration_part
score = clamp(floor(raw_score + 0.5), 0, 100)
```

The final score is an integer. Positive half values use JavaScript-compatible
rounding rather than Python's banker's rounding.

## Classification thresholds

Critical rules are evaluated first:

- `CRITICAL` when `tilt_deg >= 8`.
- `CRITICAL` when `soil_moisture_pct >= 75` and `tilt_deg >= 4`.
- `CRITICAL` when the integer score is at least `55`.

Otherwise, the result is `WARNING` when any of these are true:

- `soil_moisture_pct >= 60`
- `tilt_deg >= 4`
- `vibration_level >= 65`
- score is at least `25`

All other validated readings are `NORMAL`.

Reasons are generated in this stable order:

- `Elevated simulated moisture`
- `Elevated simulated tilt`
- `Elevated simulated vibration`
- `Demonstration score reached the warning threshold`
- `Tilt reached the critical demonstration threshold`
- `Combined moisture and tilt met the critical demonstration rule`
- `Demonstration score reached the critical threshold`

A `NORMAL` result has an empty `reasons` list. Imported `risk`,
`risk_score_demo`, and `reasons` fields are ignored.

## Python API and output

```python
from himrakshak.risk import assess_risk

assessment = assess_risk(reading)
```

The function validates the complete packet through
`himrakshak.schema.validate_reading`, does not mutate its input, and returns
exactly:

```json
{
  "risk_level": "WARNING",
  "demo_score": 48,
  "reasons": [
    "Elevated simulated moisture",
    "Elevated simulated tilt",
    "Elevated simulated vibration"
  ],
  "model_id": "himrakshak-demo-risk-engine-v1",
  "illustrative": true
}
```

Invalid or incomplete packets raise the canonical
`himrakshak.schema.SchemaValidationError`; the risk engine does not wrap or
replace schema validation errors. Simulator readings are returned after
canonical validation, so generated UTC timestamps use the normalized trailing
`Z` representation.

## Simulator scenarios

The standard-library simulator uses this interface:

```python
from himrakshak.simulator import SensorSimulator

simulator = SensorSimulator(scenario="heavy_rain", node_id="HR-01", seed=42)
packet = simulator.next_packet()
simulator.reset()
```

It emits canonical readings with UTC timestamps, `sequence` values starting at
zero, and `transport: "PYTHON_SIMULATOR"`. Generated timestamps are normalized
to UTC with a trailing `Z`. The scenario baselines are:

| Scenario | Moisture | Tilt | Vibration | Battery |
|---|---:|---:|---:|---:|
| `normal` | 32.0 | 1.2 | 12.0 | 82.0 |
| `heavy_rain` | 78.0 | 4.5 | 30.0 | 80.0 |
| `slope_shift` | 70.0 | 9.0 | 55.0 | 78.0 |
| `vibration_burst` | 40.0 | 1.5 | 88.0 | 85.0 |

Each packet applies seeded `random.Random(seed)` jitter in call order:
moisture ±1.5, tilt ±0.3, vibration ±2.0, and battery ±1.0. Values are
rounded to one decimal place. The same seed reproduces sensor readings, while
timestamps vary naturally between executions.

Run one packet:

```powershell
python -m himrakshak.simulator --scenario normal
```

Run three heavy-rain packets:

```powershell
python -m himrakshak.simulator --scenario heavy_rain --count 3 --seed 42
```

Each output line is compact JSON Lines with separate `reading` and
`assessment` objects. The assessment uses exactly the result fields shown
above. Counts must be positive.

## Tests

Run all standard-library tests from the project root:

```powershell
python -m unittest discover -s tests -v
```

## Limitations

The simulator creates synthetic values only. It does not use physical sensors,
real weather, live data, hardware libraries, network access, LoRa radio,
machine learning, or an API. Thresholds are illustrative demonstration rules
and provide no guarantee that a landslide will or will not occur.
