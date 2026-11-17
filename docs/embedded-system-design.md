# Embedded system design

The Wokwi circuit uses an ESP32 DevKit v1, three potentiometers, a pushbutton,
three resistive LED channels, and a piezo buzzer. Potentiometers substitute for
moisture, tilt, and battery inputs; the button generates a vibration burst.
These are simulated controls, not field measurements.

The firmware uses ADC validation, exponential filtering (`alpha = 0.25`),
stuck-input detection, a bounded RAM queue, exponential retry backoff, verified
GTS Root R4 TLS, and local indicators that continue operating when Wi-Fi is
unavailable. Queue contents are not persistent across reset.
