# Virtual hardware guide

Open `wokwi_esp32_demo/diagram.json` at [wokwi.com](https://wokwi.com/), copy
the desired sender profile over `station_config.h`, and start the simulation.
The Serial Monitor runs at 115200 baud.

GPIO map: moisture 34, tilt 35, battery 32, vibration button 18, green/yellow/
red LEDs 25/26/27, and buzzer 23. The landing page reads public project URLs
from `wokwi-config.js`. Empty entries show setup instructions rather than a
broken iframe. Only HTTPS Wokwi URLs are accepted by the embed helper.
