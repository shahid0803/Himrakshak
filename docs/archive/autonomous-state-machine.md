# Autonomous state machine

The state machine is local to the ESP32 and does not require the dashboard.
Sensor faults take precedence over risk classification. Critical and warning
states use hysteresis bands before returning to normal, reducing rapid
switching near demonstration thresholds. A queued packet plus a disconnected
Wi-Fi link enters `COMMUNICATION_DEGRADED`; reconnecting enters `RECOVERY`
before the next stable normal/warning/critical decision.

This is an engineering demonstration of control flow, not a validated safety
controller or a scientific landslide predictor.
