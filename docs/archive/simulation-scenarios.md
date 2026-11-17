# Simulation scenarios

1. **Normal:** baseline potentiometer values; green LED and no critical tone.
2. **Moisture increase:** turn moisture upward and observe filtered response.
3. **Slope shift:** increase tilt; warning or critical state is local.
4. **Vibration burst:** press the blue button; the vibration input spikes.
5. **Critical combination:** combine high moisture and tilt; red LED/buzzer.
6. **Sensor fault:** disconnect or drive an ADC input to an invalid rail and
   inspect sensor-health diagnostics.
7. **Network failure:** stop Wi-Fi or use the backend disconnect control; local
   indicators continue and packets remain in the bounded RAM queue.
8. **Recovery:** restore the link and observe retry/backoff delivery.

Wokwi compilation, live networking, and public project embedding are manual
checks because they require the external Wokwi service and published URLs.
