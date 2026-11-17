# HimRakshak architecture

HimRakshak is an autonomous embedded-system demonstration. Virtual sensor
interfaces feed an ESP32 controller, which validates and filters inputs,
evaluates an explainable rule set, drives local indicators, and optionally
delivers telemetry over verified HTTPS.

```text
virtual inputs -> ADC acquisition -> filtering/health -> rule engine
       -> state machine -> LEDs/buzzer
       -> Wi-Fi/HTTPS -> bounded retry queue -> Python dashboard
```

The Python service remains the backend source of truth for accepted packets,
station sequence ordering, duplicate/stale rejection, and software fault
injection. HR-01, HR-02, and HR-03 are independent virtual nodes. Browser
standalone mode is intentionally separate from Wokwi and backend-connected
telemetry.
