#pragma once

// Copy one sender profile from sender_profiles/ over this file before
// starting Wokwi. The default profile is a normal HR-01 sender.
#define HIMRAKSHAK_STATION_ID "HR-01"
#define HIMRAKSHAK_SCENARIO "normal"
#define HIMRAKSHAK_USE_SCENARIO_PRESET 0

// Set to 1 only when deliberately demonstrating the SENSOR_FAULT state.
// Return to 0 for the normal simulation.
#define HIMRAKSHAK_SIMULATE_SENSOR_FAULT 0
