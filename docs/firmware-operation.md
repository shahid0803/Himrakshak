# Firmware operation

At boot the controller reports `[HIMRAKSHAK BOOT]` and initialization status.
Each loop samples ADC inputs, applies filtering, reports sensor health, runs
the rule-based decision engine, transitions the autonomous state machine, and
updates LEDs/buzzer before attempting telemetry delivery.

Local states are `BOOT`, `INITIALIZING`, `NORMAL`, `WARNING`, `CRITICAL`,
`SENSOR_FAULT`, `COMMUNICATION_DEGRADED`, and `RECOVERY`. State transitions are
printed to Serial Monitor. Critical conditions pulse the buzzer rapidly;
warning and degraded communication use a slower acknowledgement pattern.
