#pragma once

// Remote commands arrive from the HimRakshak backend while Wokwi is running.
// Keep one station ID per Wokwi project/sender profile.
#define HIMRAKSHAK_STATION_ID "HR-01"
#define HIMRAKSHAK_SCENARIO "normal"
#define HIMRAKSHAK_USE_SCENARIO_PRESET 1
#define HIMRAKSHAK_SIMULATE_SENSOR_FAULT 0
#define HIMRAKSHAK_DEMO_BATTERY_PCT 88

// 1 = compact (recommended), 2 = detailed diagnostics.
#define HIMRAKSHAK_LOG_LEVEL 1
#define HIMRAKSHAK_REMOTE_CONTROL_ENABLED 1
