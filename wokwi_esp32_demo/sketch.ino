/*
  HimRakshak virtual ESP32 sender.

  This is software simulation. It does not use physical sensors, LoRa, or
  authenticated device credentials. The public ingestion endpoint is a
  temporary academic demonstration endpoint.
*/
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gts_root_r4.h"
#include "station_config.h"
#include "HimRakshakModel.h"

// ADC1 pins remain available while Wi-Fi is active.
const int RAINFALL_PIN = 33;
const int MOISTURE_PIN = 34;          // surface moisture
const int TILT_PIN = 35;              // primary tilt axis
const int SOIL_TEMPERATURE_PIN = 32; // virtual soil-temperature input
const int SUBSURFACE_MOISTURE_PIN = 36;
const int PORE_PRESSURE_PIN = 39;
const int VIBRATION_PIN = 18;          // discrete virtual event button
const int GREEN_LED = 25;
const int YELLOW_LED = 26;
const int RED_LED = 27;
const int BUZZER_PIN = 23;

const char *WIFI_SSID = "Wokwi-GUEST";
const char *WIFI_PASSWORD = "";
const char *TLS_HOST = "himrakshak.onrender.com";
const char *INGEST_URL = "https://himrakshak.onrender.com/api/stations/";
const unsigned long TELEMETRY_INTERVAL_MS = 10000;
const unsigned long SENSOR_SAMPLE_INTERVAL_MS = 250;
const unsigned long DIAGNOSTIC_INTERVAL_MS = 1000;
const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long NTP_TIMEOUT_MS = 15000;
const unsigned long NETWORK_SYNC_RETRY_MS = 30000;
const unsigned long RETRY_BASE_MS = 5000;
const unsigned long RETRY_MAX_MS = 60000;
const size_t MAX_PENDING_PACKETS = 10;

#ifndef HIMRAKSHAK_SIMULATE_SENSOR_FAULT
#define HIMRAKSHAK_SIMULATE_SENSOR_FAULT 0
#endif

struct Assessment {
  int score;
  const char *level;
  int tinymlPrediction;
  bool tinymlReady;
};

// Environmental risk and network health are deliberately separate states.
enum ControllerState {
  BOOT,
  INITIALIZING,
  NORMAL,
  WARNING,
  CRITICAL,
  SENSOR_FAULT,
  RECOVERY
};

enum CommunicationState {
  COMMUNICATION_UNKNOWN,
  COMMUNICATION_CONNECTED,
  COMMUNICATION_DEGRADED
};
// COMMUNICATION_DEGRADED is transport health only, never a risk-controller state.

struct SensorFrame {
  // Legacy compatibility fields retained for V1 readers.
  int moisture;
  float tiltDeg;
  int vibration;
  int battery;
  bool healthy;

  // Eight environmental parameter groups (virtual/demo provenance is explicit).
  float rainfallIntensityMmH;
  float rainfall1hMm;
  float rainfall24hMm;
  float subsurfaceMoisturePct;
  float porePressureKpa;
  float tiltXDeg;
  float tiltYDeg;
  float tiltRateDegPerHour;
  float groundDisplacementMm;
  float groundDisplacementRateMmPerHour;
  float vibrationRmsG;
  float vibrationPeakG;
  float vibrationDominantHz;
  float soilTemperatureC;
  float groundwaterDepthM;
  bool rainfallHistoryValid;
  bool groundwaterValid;
};

ControllerState controllerState = BOOT;
ControllerState previousState = BOOT;
CommunicationState communicationState = COMMUNICATION_UNKNOWN;
SensorFrame filteredFrame = {};
unsigned long lastSensorSample = 0;
unsigned long lastDiagnosticLog = 0;
unsigned long recoveryUntil = 0;
bool sensorFilterInitialized = false;
const float FILTER_ALPHA = 0.25f;
const int ADC_MIN_VALID = 0;
const int ADC_MAX_VALID = 4095;
bool sensorReadHealthy = true;

bool wifiAttemptInProgress = false;
unsigned long wifiAttemptStartedAt = 0;
bool ntpConfigurationStarted = false;
bool timeSynchronized = false;
unsigned long ntpStartedAt = 0;
bool startupSequenceSynchronized = false;
unsigned long lastStartupSyncAttempt = 0;

struct PendingPacket {
  String json;
  unsigned long sequence;
  unsigned long retryAt;
  unsigned long retryDelay;
  bool occupied;
};

PendingPacket pending[MAX_PENDING_PACKETS];
size_t pendingHead = 0;
size_t pendingCount = 0;
unsigned long sequenceNumber = 0;
unsigned long lastReport = 0;
unsigned long lastBeep = 0;
unsigned long packetsAttempted = 0;
unsigned long packetsDelivered = 0;
unsigned long failedRequests = 0;
unsigned long retryCount = 0;
unsigned long alreadyDeliveredPackets = 0;
unsigned long supersededPackets = 0;
unsigned long blockedPackets = 0;
unsigned long nextWiFiAttempt = 0;
String lastError = "";
String lastDelivery = "never";
String lastResponse = "";

struct BackendStationState {
  bool retrieved;
  bool receiverConnected;
  bool hasLatest;
  unsigned long latestSequence;
  String latestReading;
};

// Accelerated, synthetic rolling history: one real second represents one
// simulated minute. Seed values are for Wokwi demonstration only.
float rainfallMinuteBuckets[60] = {};
float rainfallHourBuckets[24] = {};
size_t rainfallMinuteIndex = 0;
size_t rainfallHourIndex = 0;
unsigned int rainfallMinuteCount = 0;
float rainfallCurrentHourMm = 0.0f;
unsigned long rainfallLastTick = 0;
bool rainfallHistorySeeded = false;
float previousTiltForRate = 0.0f;
float previousDisplacementForRate = 0.0f;
unsigned long previousDerivedSampleAt = 0;
float smoothedTiltRate = 0.0f;
float smoothedDisplacementRate = 0.0f;

bool validAdc(int value) {
  return value >= ADC_MIN_VALID && value <= ADC_MAX_VALID;
}

float sumBuckets(const float *values, size_t count) {
  float total = 0.0f;
  for (size_t index = 0; index < count; ++index) total += values[index];
  return total;
}

void seedRainfallHistory() {
  float minuteSeed = 0.035f;
  float hourSeed = 2.1f;
  if (strcmp(HIMRAKSHAK_SCENARIO, "heavy_rain") == 0) {
    // Synthetic HR-02 history: approximately 14.7 mm in the seeded hour
    // window and 103.6 mm across the seeded 24-hour window.
    minuteSeed = 0.24535f;
    hourSeed = 4.3147f;
  } else if (strcmp(HIMRAKSHAK_SCENARIO, "slope_shift") == 0) {
    minuteSeed = 0.13f;
    hourSeed = 7.8f;
  }
  for (size_t index = 0; index < 60; ++index) rainfallMinuteBuckets[index] = minuteSeed;
  for (size_t index = 0; index < 24; ++index) rainfallHourBuckets[index] = hourSeed;
  rainfallMinuteIndex = 0;
  rainfallHourIndex = 0;
  rainfallMinuteCount = 0;
  rainfallCurrentHourMm = 0.0f;
  rainfallLastTick = millis();
  rainfallHistorySeeded = true;
}

void updateRainfallHistory(float rainfallIntensityMmH) {
  if (!rainfallHistorySeeded) seedRainfallHistory();
  unsigned long now = millis();
  // Catch up at most 60 ticks after a scheduling pause; avoid an unbounded loop.
  unsigned int catchup = 0;
  while (now - rainfallLastTick >= 1000 && catchup < 60) {
    rainfallLastTick += 1000;
    const float simulatedMinuteDepth = max(0.0f, rainfallIntensityMmH) / 60.0f;
    rainfallMinuteBuckets[rainfallMinuteIndex] = simulatedMinuteDepth;
    rainfallMinuteIndex = (rainfallMinuteIndex + 1) % 60;
    rainfallCurrentHourMm += simulatedMinuteDepth;
    rainfallMinuteCount++;
    if (rainfallMinuteCount >= 60) {
      rainfallHourBuckets[rainfallHourIndex] = rainfallCurrentHourMm;
      rainfallHourIndex = (rainfallHourIndex + 1) % 24;
      rainfallMinuteCount = 0;
      rainfallCurrentHourMm = 0.0f;
    }
    catchup++;
  }
}

void readSensors(SensorFrame &frame) {
  const int rainRaw = analogRead(RAINFALL_PIN);
  const int moistureRaw = analogRead(MOISTURE_PIN);
  const int tiltRaw = analogRead(TILT_PIN);
  const int soilTempRaw = analogRead(SOIL_TEMPERATURE_PIN);
  const int subsurfaceRaw = analogRead(SUBSURFACE_MOISTURE_PIN);
  const int poreRaw = analogRead(PORE_PRESSURE_PIN);

  bool healthy = validAdc(rainRaw) && validAdc(moistureRaw) &&
                 validAdc(tiltRaw) && validAdc(soilTempRaw) &&
                 validAdc(subsurfaceRaw) && validAdc(poreRaw);
#if HIMRAKSHAK_SIMULATE_SENSOR_FAULT
  healthy = false;
#endif

  frame.rainfallIntensityMmH = map(constrain(rainRaw, 0, 4095), 0, 4095, 0, 800) / 10.0f;
  frame.moisture = map(constrain(moistureRaw, 0, 4095), 0, 4095, 0, 100);
  frame.tiltDeg = map(constrain(tiltRaw, 0, 4095), 0, 4095, 0, 150) / 10.0f;
  frame.battery = HIMRAKSHAK_DEMO_BATTERY_PCT;
  frame.subsurfaceMoisturePct = map(constrain(subsurfaceRaw, 0, 4095), 0, 4095, 0, 100);
  frame.porePressureKpa = map(constrain(poreRaw, 0, 4095), 0, 4095, 0, 160);

#if HIMRAKSHAK_USE_SCENARIO_PRESET
  if (strcmp(HIMRAKSHAK_SCENARIO, "heavy_rain") == 0) {
    frame.rainfallIntensityMmH = max(frame.rainfallIntensityMmH, 46.73f);
    frame.moisture = max(frame.moisture, 71);
    frame.subsurfaceMoisturePct = max(frame.subsurfaceMoisturePct, 55.4f);
    frame.porePressureKpa = max(frame.porePressureKpa, 72.8f);
    frame.tiltDeg = max(frame.tiltDeg, 6.5f);
  } else if (strcmp(HIMRAKSHAK_SCENARIO, "slope_shift") == 0) {
    frame.rainfallIntensityMmH = max(frame.rainfallIntensityMmH, 32.0f);
    frame.moisture = max(frame.moisture, 72);
    frame.subsurfaceMoisturePct = max(frame.subsurfaceMoisturePct, 83.0f);
    frame.porePressureKpa = max(frame.porePressureKpa, 118.0f);
    frame.tiltDeg = max(frame.tiltDeg, 9.0f);
  }
#endif

  // Virtual vibration event template. This is a button-driven event proxy,
  // not an accelerometer measurement or an FFT result.
  frame.vibration = (digitalRead(VIBRATION_PIN) == LOW) ? 85 : 12;
  frame.vibrationRmsG = (frame.vibration > 50) ? 0.48f : 0.02f;
  frame.vibrationPeakG = (frame.vibration > 50) ? 1.05f : 0.04f;
  frame.vibrationDominantHz = (frame.vibration > 50) ? 12.0f : 1.0f;

  updateRainfallHistory(frame.rainfallIntensityMmH);
  frame.rainfall1hMm = sumBuckets(rainfallMinuteBuckets, 60);
  frame.rainfall24hMm = sumBuckets(rainfallHourBuckets, 24);
  frame.rainfallHistoryValid = rainfallHistorySeeded;

  frame.tiltXDeg = frame.tiltDeg;
  // Secondary axis is a documented synthetic cross-axis proxy; no second
  // physical inclinometer channel is installed in this virtual circuit.
  frame.tiltYDeg = frame.tiltXDeg * 0.22f;
  const unsigned long now = millis();
  // One real second represents one simulated minute for this virtual demo.
  // Convert elapsed wall-clock milliseconds into accelerated simulated hours
  // so the reported deg/hour and mm/hour rates use the same demo time base as
  // the seeded rainfall-history windows.
  const float elapsedHours = previousDerivedSampleAt == 0 ? 0.0f :
      max(0.001f, (now - previousDerivedSampleAt) / 60000.0f);
  if (previousDerivedSampleAt != 0) {
    const float rawTiltRate = (frame.tiltXDeg - previousTiltForRate) / elapsedHours;
    smoothedTiltRate = 0.25f * rawTiltRate + 0.75f * smoothedTiltRate;
    const float leverArmMm = 100.0f;
    frame.groundDisplacementMm = sin(frame.tiltXDeg * PI / 180.0f) * leverArmMm;
    const float rawDisplacementRate =
        fabs(frame.groundDisplacementMm - previousDisplacementForRate) / elapsedHours;
    smoothedDisplacementRate = 0.2f * rawDisplacementRate + 0.8f * smoothedDisplacementRate;
  } else {
    frame.groundDisplacementMm = sin(frame.tiltXDeg * PI / 180.0f) * 100.0f;
  }
  frame.tiltRateDegPerHour = constrain(smoothedTiltRate, -12.0f, 12.0f);
  frame.groundDisplacementRateMmPerHour = constrain(smoothedDisplacementRate, 0.0f, 12.0f);
#if HIMRAKSHAK_USE_SCENARIO_PRESET
  // HR-02 is a synthetic heavy-rain trend scenario. These preset rates make
  // the demonstration profile exercise WARNING in both rule and TinyML paths
  // before a user manually changes the virtual tilt potentiometer. These are
  // synthetic preset values, not measured movement rates.
  if (strcmp(HIMRAKSHAK_SCENARIO, "heavy_rain") == 0) {
    frame.tiltRateDegPerHour = max(frame.tiltRateDegPerHour, 1.2f);
    frame.groundDisplacementRateMmPerHour =
        max(frame.groundDisplacementRateMmPerHour, 0.84f);
  }
#endif
  previousTiltForRate = frame.tiltXDeg;
  previousDisplacementForRate = frame.groundDisplacementMm;
  previousDerivedSampleAt = now;

  // Soil temperature is a station-profile proxy driven by a virtual analog
  // potentiometer on GPIO32; it is not a calibrated DS18B20 measurement.
  frame.soilTemperatureC = map(constrain(soilTempRaw, 0, 4095), 0, 4095, -50, 350) / 10.0f;
  // Battery remains a separate fixed device-health demonstration estimate.
  // Groundwater depth is a transparent demo proxy derived from pore pressure
  // and subsurface moisture, not a direct water-level instrument reading.
  frame.groundwaterDepthM = constrain(
      9.0f - frame.porePressureKpa * 0.032f - frame.subsurfaceMoisturePct * 0.018f,
      0.3f, 10.0f);
  frame.groundwaterValid = true;
  frame.healthy = healthy;
  sensorReadHealthy = healthy;
}

void makeTinyMlFeatures(const SensorFrame &frame, float features[16]) {
  features[0] = frame.rainfallIntensityMmH;
  features[1] = frame.rainfall1hMm;
  features[2] = frame.rainfall24hMm;
  features[3] = frame.moisture;
  features[4] = frame.subsurfaceMoisturePct;
  features[5] = frame.porePressureKpa;
  features[6] = frame.tiltXDeg;
  features[7] = frame.tiltYDeg;
  features[8] = frame.tiltRateDegPerHour;
  features[9] = frame.groundDisplacementMm;
  features[10] = frame.groundDisplacementRateMmPerHour;
  features[11] = frame.vibrationRmsG;
  features[12] = frame.vibrationPeakG;
  features[13] = frame.vibrationDominantHz;
  features[14] = frame.soilTemperatureC;
  features[15] = frame.groundwaterDepthM;
}

Assessment assessRisk(const SensorFrame &frame, int tinymlPrediction) {
  float scoreRaw =
      constrain(frame.rainfall24hMm / 220.0f, 0.0f, 1.0f) * 16.0f +
      constrain(frame.subsurfaceMoisturePct / 100.0f, 0.0f, 1.0f) * 12.0f +
      constrain(frame.porePressureKpa / 160.0f, 0.0f, 1.0f) * 18.0f +
      constrain(fabs(frame.tiltXDeg) / 15.0f, 0.0f, 1.0f) * 18.0f +
      constrain(frame.groundDisplacementRateMmPerHour / 8.0f, 0.0f, 1.0f) * 18.0f +
      constrain(frame.vibrationPeakG / 3.5f, 0.0f, 1.0f) * 8.0f +
      (1.0f - constrain(frame.groundwaterDepthM / 10.0f, 0.0f, 1.0f)) * 10.0f;
  int score = (int)constrain(round(scoreRaw), 0, 100);

  bool criticalRule = frame.tiltXDeg >= 8.5f ||
      frame.groundDisplacementRateMmPerHour >= 2.2f ||
      (frame.porePressureKpa >= 108.0f && frame.subsurfaceMoisturePct >= 72.0f) ||
      (frame.rainfall24hMm >= 145.0f && frame.moisture >= 72 && frame.porePressureKpa >= 82.0f) ||
      (frame.tiltRateDegPerHour >= 1.8f && frame.porePressureKpa >= 78.0f);
  bool warningRule =
      (frame.rainfall24hMm >= 65.0f && frame.moisture >= 54) ||
      frame.porePressureKpa >= 66.0f || frame.tiltXDeg >= 4.1f ||
      frame.groundDisplacementRateMmPerHour >= 0.55f ||
      frame.vibrationPeakG >= 0.95f || frame.groundwaterDepthM <= 1.8f ||
      frame.rainfallIntensityMmH >= 35.0f;

  int ruleClass = criticalRule ? 2 : (warningRule ? 1 : 0);
  // Conservative fusion: the ML model may raise severity, but never lowers
  // a rule-based warning/critical safety state.
  int finalClass = max(ruleClass, tinymlPrediction >= 0 ? tinymlPrediction : 0);
  const char *level = finalClass == 2 ? "CRITICAL" : (finalClass == 1 ? "WARNING" : "NORMAL");
  return {score, level, tinymlPrediction,
          frame.healthy && frame.rainfallHistoryValid && frame.groundwaterValid};
}

SensorFrame acquireFilteredSensors() {
  const unsigned long now = millis();
  if (sensorFilterInitialized && now - lastSensorSample < SENSOR_SAMPLE_INTERVAL_MS) {
    return filteredFrame;
  }
  SensorFrame raw = {};
  readSensors(raw);
  if (!sensorFilterInitialized) {
    filteredFrame = raw;
    sensorFilterInitialized = true;
  } else {
    #define BLEND_FIELD(field) filteredFrame.field = FILTER_ALPHA * raw.field + (1.0f - FILTER_ALPHA) * filteredFrame.field
    BLEND_FIELD(rainfallIntensityMmH);
    BLEND_FIELD(moisture);
    BLEND_FIELD(tiltDeg);
    BLEND_FIELD(battery);
    BLEND_FIELD(subsurfaceMoisturePct);
    BLEND_FIELD(porePressureKpa);
    BLEND_FIELD(soilTemperatureC);
    BLEND_FIELD(groundwaterDepthM);
    BLEND_FIELD(rainfall1hMm);
    BLEND_FIELD(rainfall24hMm);
    BLEND_FIELD(tiltXDeg);
    BLEND_FIELD(tiltYDeg);
    BLEND_FIELD(tiltRateDegPerHour);
    BLEND_FIELD(groundDisplacementMm);
    BLEND_FIELD(groundDisplacementRateMmPerHour);
    BLEND_FIELD(vibrationRmsG);
    BLEND_FIELD(vibrationPeakG);
    BLEND_FIELD(vibrationDominantHz);
    #undef BLEND_FIELD
    filteredFrame.moisture = (int)round(filteredFrame.moisture);
    filteredFrame.vibration = raw.vibration; // event input must remain responsive
    filteredFrame.healthy = raw.healthy;
    filteredFrame.rainfallHistoryValid = raw.rainfallHistoryValid;
    filteredFrame.groundwaterValid = raw.groundwaterValid;
  }
  filteredFrame.tiltDeg = filteredFrame.tiltXDeg;
  lastSensorSample = now;
  return filteredFrame;
}

const char *stateName(ControllerState state) {
  switch (state) {
    case BOOT: return "BOOT";
    case INITIALIZING: return "INITIALIZING";
    case NORMAL: return "NORMAL";
    case WARNING: return "WARNING";
    case CRITICAL: return "CRITICAL";
    case SENSOR_FAULT: return "SENSOR_FAULT";
    case RECOVERY: return "RECOVERY";
  }
  return "UNKNOWN";
}

const char *communicationStateName(CommunicationState state) {
  switch (state) {
    case COMMUNICATION_UNKNOWN: return "COMMUNICATION_UNKNOWN";
    case COMMUNICATION_CONNECTED: return "COMMUNICATION_CONNECTED";
    case COMMUNICATION_DEGRADED: return "COMMUNICATION_DEGRADED";
  }
  return "COMMUNICATION_UNKNOWN";
}

ControllerState evaluateState(const SensorFrame &frame, const Assessment &assessment) {
  if (!frame.healthy || !frame.rainfallHistoryValid || !frame.groundwaterValid) {
    return controllerState == CRITICAL ? CRITICAL : SENSOR_FAULT;
  }
  if (strcmp(assessment.level, "CRITICAL") == 0) return CRITICAL;
  if (controllerState == CRITICAL &&
      (frame.tiltXDeg >= 7.0f || frame.porePressureKpa >= 95.0f ||
       frame.groundDisplacementRateMmPerHour >= 1.6f || assessment.score >= 45)) {
    return CRITICAL;
  }
  if (strcmp(assessment.level, "WARNING") == 0) return WARNING;
  if (controllerState == WARNING &&
      (frame.moisture >= 55 || frame.subsurfaceMoisturePct >= 58.0f ||
       frame.porePressureKpa >= 55.0f || frame.tiltXDeg >= 3.5f ||
       assessment.score >= 20)) {
    return WARNING;
  }
  if (controllerState == SENSOR_FAULT) return RECOVERY;
  if (controllerState == RECOVERY && (long)(millis() - recoveryUntil) < 0) return RECOVERY;
  return NORMAL;
}

void transitionState(ControllerState next) {
  if (next == controllerState) return;
  previousState = controllerState;
  controllerState = next;
  if (next == RECOVERY) recoveryUntil = millis() + 1500;
  Serial.printf("[STATE TRANSITION] %s -> %s\n",
                stateName(previousState), stateName(controllerState));
}

void reportDecision(const SensorFrame &frame, const Assessment &assessment) {
  Serial.printf("[EDGE PROCESSING] score=%d rule+tinyml_level=%s ml_class=%d model_ready=%s\n",
                assessment.score, assessment.level, assessment.tinymlPrediction,
                assessment.tinymlReady ? "yes" : "no");
  Serial.printf("[GEOTECHNICAL FEATURES] rain=%.1fmm/h rain1h=%.1fmm rain24h=%.1fmm surface=%.1f%% subsurface=%.1f%% pore=%.1fkPa\n",
                frame.rainfallIntensityMmH, frame.rainfall1hMm, frame.rainfall24hMm,
                (float)frame.moisture, frame.subsurfaceMoisturePct, frame.porePressureKpa);
  Serial.printf("[MOVEMENT FEATURES] tiltX=%.2f tiltY=%.2f tiltRate=%.2fdeg/h displacementProxy=%.2fmm displacementRate=%.2fmm/h\n",
                frame.tiltXDeg, frame.tiltYDeg, frame.tiltRateDegPerHour,
                frame.groundDisplacementMm, frame.groundDisplacementRateMmPerHour);
  Serial.printf("[CONTEXT FEATURES] vibrationRMS=%.3fg peak=%.3fg freqProxy=%.1fHz soilTempProxy=%.1fC groundwaterProxy=%.2fm battery=%.0f%% health=%s\n",
                frame.vibrationRmsG, frame.vibrationPeakG, frame.vibrationDominantHz,
                frame.soilTemperatureC, frame.groundwaterDepthM, (float)frame.battery,
                frame.healthy ? "OK" : "FAULT");
}

String buildPacket(unsigned long sequence,
                   const SensorFrame &frame,
                   const Assessment &assessment) {
  String packet = "{\"node_id\":\"";
  packet += HIMRAKSHAK_STATION_ID;
  packet += "\",\"sequence\":";
  packet += sequence;
  packet += ",\"timestamp\":\"SIMULATED\",\"soil_moisture_pct\":";
  packet += frame.moisture;
  packet += ",\"tilt_deg\":";
  packet += String(fabs(frame.tiltXDeg), 2);
  packet += ",\"vibration_level\":";
  packet += frame.vibration;
  packet += ",\"battery_pct\":";
  packet += frame.battery;
  packet += ",\"sensor_schema_version\":2";
  packet += ",\"rainfall_intensity_mm_h\":" + String(frame.rainfallIntensityMmH, 2);
  packet += ",\"rainfall_1h_mm\":" + String(frame.rainfall1hMm, 2);
  packet += ",\"rainfall_24h_mm\":" + String(frame.rainfall24hMm, 2);
  packet += ",\"soil_moisture_surface_pct\":" + String((float)frame.moisture, 2);
  packet += ",\"soil_moisture_subsurface_pct\":" + String(frame.subsurfaceMoisturePct, 2);
  packet += ",\"pore_water_pressure_kpa\":" + String(frame.porePressureKpa, 2);
  packet += ",\"tilt_x_deg\":" + String(frame.tiltXDeg, 2);
  packet += ",\"tilt_y_deg\":" + String(frame.tiltYDeg, 2);
  packet += ",\"tilt_rate_deg_per_h\":" + String(frame.tiltRateDegPerHour, 3);
  packet += ",\"ground_displacement_mm\":" + String(frame.groundDisplacementMm, 2);
  packet += ",\"ground_displacement_rate_mm_per_h\":" + String(frame.groundDisplacementRateMmPerHour, 3);
  packet += ",\"vibration_rms_g\":" + String(frame.vibrationRmsG, 3);
  packet += ",\"vibration_peak_g\":" + String(frame.vibrationPeakG, 3);
  packet += ",\"vibration_dominant_hz\":" + String(frame.vibrationDominantHz, 2);
  packet += ",\"soil_temperature_c\":" + String(frame.soilTemperatureC, 2);
  packet += ",\"groundwater_depth_m\":" + String(frame.groundwaterDepthM, 3);
  packet += ",\"tinyml_prediction\":" + String(assessment.tinymlPrediction);
  packet += ",\"model_ready\":" + String(assessment.tinymlReady ? "true" : "false");
  packet += ",\"feature_inputs_valid\":" + String(frame.healthy ? "true" : "false");
  packet += ",\"rainfall_history_valid\":" + String(frame.rainfallHistoryValid ? "true" : "false");
  packet += ",\"groundwater_valid\":" + String(frame.groundwaterValid ? "true" : "false");
  packet += ",\"model_id\":\"himrakshak-synthetic-demo-tree-v2\"";
  packet += ",\"measurement_provenance\":\"WOKWI_VIRTUAL_WITH_DERIVED_PROXIES\"";
  packet += ",\"rainfall_history_mode\":\"ACCELERATED_SYNTHETIC_SEEDED_1S_PER_MIN\"";
  packet += ",\"risk\":\"";
  packet += assessment.level;
  packet += "\",\"risk_score_demo\":";
  packet += assessment.score;
  packet += ",\"sensor_health\":\"";
  packet += frame.healthy ? "OK" : "FAULT";
  packet += "\",\"transport\":\"WOKWI_SOFTWARE_SIMULATION\",\"is_simulated\":true}";
  return packet;
}

void reportStatus(const char *message) {
  Serial.printf("[%lu] %s | station=%s pending=%u attempted=%lu delivered=%lu already_delivered=%lu superseded=%lu blocked=%lu failed=%lu retries=%lu last_error=%s\n",
                millis(), message, HIMRAKSHAK_STATION_ID, (unsigned)pendingCount,
                packetsAttempted, packetsDelivered, alreadyDeliveredPackets,
                supersededPackets, blockedPackets, failedRequests, retryCount,
                lastError.length() ? lastError.c_str() : "none");
}

bool enqueuePacket(const String &json, unsigned long sequence) {
  if (pendingCount >= MAX_PENDING_PACKETS) {
    lastError = "pending queue full";
    reportStatus("QUEUE FULL; packet retained only in current generation");
    return false;
  }
  size_t slot = (pendingHead + pendingCount) % MAX_PENDING_PACKETS;
  pending[slot] = {json, sequence, millis(), RETRY_BASE_MS, true};
  pendingCount++;
  return true;
}

void removeHeadPacket() {
  if (pendingCount == 0) return;
  pending[pendingHead].json = "";
  pending[pendingHead].occupied = false;
  pendingHead = (pendingHead + 1) % MAX_PENDING_PACKETS;
  pendingCount--;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiAttemptInProgress) {
      Serial.printf("[COMMUNICATION STATUS] Wi-Fi connected, IP=%s\n",
                    WiFi.localIP().toString().c_str());
    }
    wifiAttemptInProgress = false;
    communicationState = COMMUNICATION_CONNECTED;
    return true;
  }

  unsigned long now = millis();
  if (wifiAttemptInProgress && now - wifiAttemptStartedAt < WIFI_TIMEOUT_MS) {
    communicationState = COMMUNICATION_DEGRADED;
    return false;
  }
  if ((long)(now - nextWiFiAttempt) < 0) {
    communicationState = COMMUNICATION_DEGRADED;
    return false;
  }

  if (wifiAttemptInProgress) {
    Serial.println("[COMMUNICATION STATUS] Wi-Fi attempt timed out; scheduling retry");
    WiFi.disconnect(false, false);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiAttemptInProgress = true;
  wifiAttemptStartedAt = now;
  nextWiFiAttempt = now + WIFI_TIMEOUT_MS + RETRY_BASE_MS;
  communicationState = COMMUNICATION_DEGRADED;
  Serial.printf("[COMMUNICATION STATUS] Starting non-blocking Wi-Fi connection to %s\n",
                WIFI_SSID);
  return false;
}

bool synchronizeClock() {
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!ntpConfigurationStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ntpConfigurationStarted = true;
    ntpStartedAt = millis();
    Serial.println("[TIME SYNC] NTP synchronization started");
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    if (!timeSynchronized) {
      Serial.printf("[TIME SYNC] NTP synchronized: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                    timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                    timeinfo.tm_sec);
    }
    timeSynchronized = true;
    return true;
  }

  if (millis() - ntpStartedAt >= NTP_TIMEOUT_MS) {
    lastError = "NTP synchronization pending; HTTPS held until clock is valid";
  }
  return false;
}

String jsonValue(const String &json, const char *key) {
  String marker = String("\"") + key + "\"";
  int keyPos = json.indexOf(marker);
  if (keyPos < 0) return "";
  int colon = json.indexOf(':', keyPos + marker.length());
  if (colon < 0) return "";
  int start = colon + 1;
  while (start < (int)json.length() && isspace(json[start])) start++;
  if (start >= (int)json.length()) return "";
  if (json[start] == '"') {
    int end = start + 1;
    while (end < (int)json.length()) {
      if (json[end] == '"' && json[end - 1] != '\\') {
        return json.substring(start + 1, end);
      }
      end++;
    }
    return "";
  }
  int end = start;
  while (end < (int)json.length() && json[end] != ',' && json[end] != '}' &&
         json[end] != ']' && !isspace(json[end])) {
    end++;
  }
  return json.substring(start, end);
}

String jsonObjectValue(const String &json, const char *key) {
  String marker = String("\"") + key + "\"";
  int keyPos = json.indexOf(marker);
  if (keyPos < 0) return "";
  int colon = json.indexOf(':', keyPos + marker.length());
  if (colon < 0) return "";
  int start = colon + 1;
  while (start < (int)json.length() && isspace(json[start])) start++;
  if (start >= (int)json.length() || json[start] != '{') return "";
  int depth = 0;
  bool inString = false;
  for (int i = start; i < (int)json.length(); i++) {
    char ch = json[i];
    if (ch == '"' && (i == start || json[i - 1] != '\\')) inString = !inString;
    if (inString) continue;
    if (ch == '{') depth++;
    if (ch == '}' && --depth == 0) return json.substring(start, i + 1);
  }
  return "";
}

bool parseUnsigned(const String &value, unsigned long &result) {
  if (value.length() == 0) return false;
  for (size_t i = 0; i < value.length(); i++) {
    if (!isDigit(value[i])) return false;
  }
  result = strtoul(value.c_str(), nullptr, 10);
  return true;
}

bool readingsMatch(const String &pendingJson, const String &backendReading) {
  const char *fields[] = {
      "node_id", "sequence", "timestamp", "soil_moisture_pct",
      "tilt_deg", "vibration_level", "battery_pct", "transport"};
  for (const char *field : fields) {
    if (jsonValue(pendingJson, field) != jsonValue(backendReading, field)) {
      return false;
    }
  }
  return true;
}

void diagnoseTransportFailure() {
  IPAddress address;
  if (WiFi.hostByName(TLS_HOST, address) != 1) {
    lastError = "DNS failure resolving " + String(TLS_HOST);
    return;
  }

  WiFiClient tcp;
  tcp.setTimeout(8000);
  if (!tcp.connect(address, 443)) {
    lastError = "TCP failure connecting " + address.toString() + ":443";
    tcp.stop();
    return;
  }
  tcp.stop();

  WiFiClientSecure tls;
  tls.setCACert(HIMRAKSHAK_ROOT_CA);
  tls.setTimeout(8000);
  if (!tls.connect(TLS_HOST, 443)) {
    char detail[128] = {};
    tls.lastError(detail, sizeof(detail));
    lastError = "TLS failure verifying " + String(TLS_HOST);
    if (detail[0] != '\0') {
      lastError += ": ";
      lastError += detail;
    }
    tls.stop();
    return;
  }
  tls.stop();
  lastError = "HTTPS request failed after DNS, TCP, and TLS succeeded";
}

bool getStationState(BackendStationState &state) {
  state = {false, true, false, 0, ""};
  if (WiFi.status() != WL_CONNECTED || !timeSynchronized) return false;
  WiFiClientSecure client;
  client.setCACert(HIMRAKSHAK_ROOT_CA);
  client.setTimeout(8000);
  HTTPClient https;
  String url = String(INGEST_URL) + HIMRAKSHAK_STATION_ID;
  if (!https.begin(client, url)) {
    diagnoseTransportFailure();
    if (lastError.startsWith("HTTPS request")) {
      lastError = "HTTP client begin failed while reading station state";
    }
    return false;
  }
  https.setConnectTimeout(8000);
  https.setTimeout(8000);
  int status = https.GET();
  String response = https.getString();
  https.end();
  if (status != 200) {
    if (status < 0) {
      diagnoseTransportFailure();
    } else {
      lastError = String("HTTP GET station state status ") + status;
    }
    return false;
  }
  String station = jsonObjectValue(response, "station");
  if (station.length() == 0) {
    lastError = "GET station state missing station object";
    return false;
  }
  state.retrieved = true;
  state.receiverConnected = jsonValue(station, "receiver_connected") != "false";
  String latest = jsonObjectValue(station, "latest");
  if (latest.length() == 0 || latest == "null") return true;
  state.latestReading = jsonObjectValue(latest, "reading");
  String latestSequence = jsonValue(state.latestReading, "sequence");
  state.hasLatest = parseUnsigned(latestSequence, state.latestSequence);
  if (!state.hasLatest) {
    lastError = "GET station state has invalid latest sequence";
    state.retrieved = false;
  }
  return state.retrieved;
}

bool replacePacketSequence(PendingPacket &packet, unsigned long newSequence) {
  const String marker = "\"sequence\":";
  int markerPosition = packet.json.indexOf(marker);
  if (markerPosition < 0) return false;

  int valueStart = markerPosition + marker.length();
  int valueEnd = valueStart;
  while (valueEnd < (int)packet.json.length() &&
         isDigit(packet.json[valueEnd])) {
    valueEnd++;
  }
  if (valueEnd == valueStart) return false;

  packet.json = packet.json.substring(0, valueStart) +
                String(newSequence) + packet.json.substring(valueEnd);
  packet.sequence = newSequence;
  return true;
}

bool rebaseQueuedSequences(unsigned long firstSequence) {
  for (size_t i = 0; i < pendingCount; i++) {
    size_t slot = (pendingHead + i) % MAX_PENDING_PACKETS;
    if (!replacePacketSequence(pending[slot], firstSequence)) {
      lastError = "unable to rebase queued packet sequence";
      return false;
    }
    firstSequence++;
  }
  if (sequenceNumber < firstSequence) sequenceNumber = firstSequence;
  return true;
}

bool synchronizeStartupSequence() {
  BackendStationState state;
  if (!getStationState(state)) {
    reportStatus("Station state unavailable; local queue retained until synchronization");
    return false;
  }

  if (state.hasLatest) {
    unsigned long backendNext = state.latestSequence + 1;
    bool queuedSequenceIsStale =
        pendingCount > 0 && pending[pendingHead].sequence <= state.latestSequence;

    if (sequenceNumber < backendNext || queuedSequenceIsStale) {
      unsigned long firstSequence = backendNext;
      if (!rebaseQueuedSequences(firstSequence)) return false;
      Serial.printf("[SEQUENCE SYNC] Rebased queued packets from backend sequence %lu\n",
                    state.latestSequence);
    } else {
      Serial.printf("[SEQUENCE SYNC] Local sequence %lu is ahead of backend sequence %lu\n",
                    sequenceNumber, state.latestSequence);
    }
  } else {
    Serial.printf("[SEQUENCE SYNC] No accepted backend reading; using local sequence %lu\n",
                  sequenceNumber);
  }

  startupSequenceSynchronized = true;
  return true;
}

int postPacket(PendingPacket &packet) {
  packetsAttempted++;
  if (WiFi.status() != WL_CONNECTED || !timeSynchronized ||
      !startupSequenceSynchronized) return -1;
  WiFiClientSecure client;
  client.setCACert(HIMRAKSHAK_ROOT_CA);
  client.setTimeout(8000);
  HTTPClient https;
  String url = String(INGEST_URL) + HIMRAKSHAK_STATION_ID + "/readings";
  if (!https.begin(client, url)) {
    diagnoseTransportFailure();
    if (lastError.startsWith("HTTPS request")) {
      lastError = "HTTP client begin failed";
    }
    failedRequests++;
    return -1;
  }
  https.setConnectTimeout(8000);
  https.setTimeout(8000);
  https.addHeader("Content-Type", "application/json");
  Serial.printf("POST seq=%lu payload=%s\n", packet.sequence, packet.json.c_str());
  int status = https.POST(packet.json);
  String response = https.getString();
  https.end();
  lastResponse = response;
  Serial.printf("HTTPS status=%d response=%s\n", status, response.c_str());
  if (status >= 200 && status < 300) {
    packetsDelivered++;
    lastDelivery = String(millis());
    return status;
  }
  failedRequests++;
  if (status == 409) {
    lastError = "409 duplicate/stale; reconcile with GET station state";
  } else if (status == 429) {
    lastError = "429 rate limited";
  } else if (status > 0) {
    lastError = String("HTTP POST status ") + status;
  } else {
    diagnoseTransportFailure();
  }
  return status;
}

bool reconcileConflict(PendingPacket &packet) {
  String classification = jsonValue(lastResponse, "classification");
  if (classification != "duplicate" && classification != "stale") {
    blockedPackets++;
    lastError = "409 conflict is not duplicate/stale; packet retained";
    reportStatus("409 conflict requires manual inspection; packet retained");
    return false;
  }
  BackendStationState state;
  if (!getStationState(state)) {
    lastError = "409 reconciliation GET failed; packet retained";
    reportStatus("409 reconciliation unavailable; packet retained");
    return false;
  }
  if (!state.receiverConnected) {
    lastError = "receiver disconnected during 409 reconciliation";
    reportStatus("Receiver disconnected; packet retained for retry");
    return false;
  }
  if (!state.hasLatest) {
    blockedPackets++;
    lastError = "409 reconciliation returned no accepted reading";
    reportStatus("No backend reading to reconcile; packet retained");
    return false;
  }
  if (packet.sequence == state.latestSequence &&
      readingsMatch(packet.json, state.latestReading)) {
    alreadyDeliveredPackets++;
    lastDelivery = String(millis());
    lastError = "packet already delivered";
    reportStatus("Packet already delivered; removing matching duplicate");
    return true;
  }
  if (packet.sequence < state.latestSequence) {
    supersededPackets++;
    lastError = "packet superseded by newer backend sequence";
    Serial.printf("WARNING: superseded packet seq=%lu backend_seq=%lu; removing from queue\n",
                  packet.sequence, state.latestSequence);
    reportStatus("Superseded packet recorded and removed");
    return true;
  }
  if (packet.sequence == state.latestSequence) {
    blockedPackets++;
    lastError = "sequence reuse conflict with different reading";
    reportStatus("Sequence reuse conflict; packet retained for manual resolution");
    return false;
  }
  blockedPackets++;
  lastError = "pending sequence is newer than backend sequence";
  reportStatus("Pending packet is newer; retaining unresolved conflict");
  return false;
}

void flushQueue() {
  if (pendingCount == 0 || WiFi.status() != WL_CONNECTED) return;
  PendingPacket &packet = pending[pendingHead];
  if ((long)(millis() - packet.retryAt) < 0) return;
  int status = postPacket(packet);
  if (status >= 200 && status < 300) {
    removeHeadPacket();
    return;
  }
  if (status == 409) {
    if (reconcileConflict(packet)) {
      removeHeadPacket();
      return;
    }
    packet.retryDelay = min(packet.retryDelay * 2, RETRY_MAX_MS);
  } else {
    retryCount++;
    packet.retryDelay = min(packet.retryDelay * 2, RETRY_MAX_MS);
  }
  packet.retryAt = millis() + packet.retryDelay;
  reportStatus("Delivery deferred; backing off");
}

void updateIndicators() {
  unsigned long now = millis();
  bool green = false;
  bool yellow = false;
  bool red = false;
  bool beep = false;
  unsigned int beepFrequency = 0;
  unsigned long beepInterval = 0;
  unsigned int beepDuration = 0;

  switch (controllerState) {
    case NORMAL:
      green = true;
      break;
    case WARNING:
      yellow = true;
      beep = true;
      beepFrequency = 900;
      beepInterval = 1800;
      beepDuration = 80;
      break;
    case CRITICAL:
      red = true;
      // A simultaneous sensor fault is shown by adding a blinking yellow LED
      // while retaining the red critical indication and critical buzzer.
      yellow = !sensorReadHealthy && ((now / 400) % 2) == 0;
      beep = true;
      beepFrequency = 1800;
      beepInterval = 900;
      beepDuration = 180;
      break;
    case SENSOR_FAULT:
      yellow = ((now / 400) % 2) == 0;
      beep = true;
      beepFrequency = 650;
      beepInterval = 2500;
      beepDuration = 100;
      break;
    case RECOVERY:
      green = ((now / 350) % 2) == 0;
      break;
    case BOOT:
    case INITIALIZING:
      break;
  }

  digitalWrite(GREEN_LED, green ? HIGH : LOW);
  digitalWrite(YELLOW_LED, yellow ? HIGH : LOW);
  digitalWrite(RED_LED, red ? HIGH : LOW);

  if (beep && now - lastBeep >= beepInterval) {
    tone(BUZZER_PIN, beepFrequency, beepDuration);
    lastBeep = now;
  } else if (!beep) {
    noTone(BUZZER_PIN);
  }
}

void reportCommunicationState() {
  CommunicationState next =
      (WiFi.status() == WL_CONNECTED && timeSynchronized &&
       startupSequenceSynchronized)
          ? COMMUNICATION_CONNECTED
          : COMMUNICATION_DEGRADED;
  if (next != communicationState) {
    communicationState = next;
    Serial.printf("[COMMUNICATION STATUS] %s\n",
                  communicationStateName(communicationState));
  }
}

void serviceNetwork() {
  if (!connectWiFi()) {
    reportCommunicationState();
    return;
  }

  if (!synchronizeClock()) {
    reportCommunicationState();
    return;
  }

  if (!startupSequenceSynchronized) {
    unsigned long now = millis();
    if (lastStartupSyncAttempt == 0 ||
        now - lastStartupSyncAttempt >= NETWORK_SYNC_RETRY_MS) {
      lastStartupSyncAttempt = now;
      startupSequenceSynchronized = synchronizeStartupSequence();
    }
    if (!startupSequenceSynchronized) {
      reportCommunicationState();
      return;
    }
  }

  flushQueue();
  reportCommunicationState();
}

void setup() {
  Serial.begin(115200);
  Serial.println("[HIMRAKSHAK BOOT]");
  controllerState = INITIALIZING;
  Serial.println("[ESP32 INITIALIZED]");

  pinMode(VIBRATION_PIN, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  analogReadResolution(12);
  pinMode(RAINFALL_PIN, INPUT);
  pinMode(SUBSURFACE_MOISTURE_PIN, INPUT);
  pinMode(PORE_PRESSURE_PIN, INPUT);
  seedRainfallHistory();

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);
  noTone(BUZZER_PIN);

  Serial.printf("HimRakshak sender %s starting; scenario=%s; interval=%lus\n",
                HIMRAKSHAK_STATION_ID, HIMRAKSHAK_SCENARIO,
                TELEMETRY_INTERVAL_MS / 1000);
  Serial.printf("[COMMUNICATION STATUS] queue_capacity=%u tls=verified\n",
                (unsigned)MAX_PENDING_PACKETS);
  Serial.printf("[TINYML INITIALIZED] model=%s features=16 classes=NORMAL/WARNING/CRITICAL\n",
                HimRakshakTinyML::MODEL_ID);
  Serial.println("[PROVENANCE] virtual potentiometers + derived proxies; synthetic training labels");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Start Wi-Fi without waiting. Local sensor processing begins immediately.
  connectWiFi();
}

void loop() {
  SensorFrame frame = acquireFilteredSensors();
  float featureVector[16] = {};
  makeTinyMlFeatures(frame, featureVector);
  const bool modelReady = frame.healthy && frame.rainfallHistoryValid && frame.groundwaterValid;
  const int tinymlPrediction = modelReady ? HimRakshakTinyML::predict(featureVector) : -1;
  Assessment assessment = assessRisk(frame, tinymlPrediction);
  ControllerState nextState = evaluateState(frame, assessment);
  transitionState(nextState);
  updateIndicators();

  unsigned long now = millis();
  if (now - lastDiagnosticLog >= DIAGNOSTIC_INTERVAL_MS) {
    lastDiagnosticLog = now;
    Serial.printf("[TINYML INFERENCE] ready=%s prediction=%d model=%s\n",
                  modelReady ? "YES" : "NO", tinymlPrediction, HimRakshakTinyML::MODEL_ID);
    reportDecision(frame, assessment);
  }

  if (now - lastReport >= TELEMETRY_INTERVAL_MS) {
    lastReport = now;
    String packet = buildPacket(sequenceNumber, frame, assessment);
    Serial.printf("[AUTONOMOUS RESPONSE] risk_state=%s local_leds_buzzer=updated\n",
                  stateName(controllerState));
    Serial.printf("[TELEMETRY DELIVERY] sequence=%lu battery=%d%% risk=%s score=%d tinyml=%d\n",
                  sequenceNumber, frame.battery, assessment.level,
                  assessment.score, assessment.tinymlPrediction);
    if (enqueuePacket(packet, sequenceNumber)) {
      sequenceNumber++;
      reportStatus("V2 packet queued for certificate-verified HTTPS delivery");
    }
  }

  serviceNetwork();
  delay(20);
}
