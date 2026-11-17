# Testing and evidence

Run the focused local validation from the repository root:

```powershell
python -B -m unittest discover -s tests -v
python -m compileall -q himrakshak tests scripts
```

The tests cover schema and API compatibility, V2 features/model behavior,
station isolation and sequence handling, simulated reliability, data
intelligence, frontend contracts, and firmware contracts. The compileall
command checks Python syntax only.

Manual evidence still required:

- compile and run the ESP32 sketch in Wokwi;
- verify six virtual inputs, LEDs, buzzer, Wi-Fi/TLS, and telemetry;
- verify any published Wokwi URLs and cloud deployment;
- exercise browser layout and external data-provider access.

Historical phase reports and regression checklists are preserved in
[docs/archive](archive/README.md). Their test counts and dates are historical
evidence, not current release claims.
