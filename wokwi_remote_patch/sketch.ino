/*
 * HimRakshak V2 - upgraded ESP32 edge-intelligence sender
 *
 * Sensor acquisition and feature engineering run in a high-priority FreeRTOS
 * task so synchronous HTTPS operations in loop() do not interrupt MPU6050
 * sampling. Live mode targets 100 Hz; virtual scenarios target 50 Hz to tolerate
 * Wokwi scheduler jitter while preserving the scenario signal bandwidth. The TinyML model executes locally on the ESP32.
 *
 * Demonstration limitations:
 * - Wokwi inputs and scenario signals are simulated, not field measurements.
 * - The Decision Tree was trained on synthetic demonstration data.
 * - The 24-hour rainfall input is not model-ready until a complete rolling
 *   24-hour (accelerated simulation-time) window has been accumulated.
 * - No LoRa radio or authenticated field-device identity is implemented here.
 */

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "network_types.h"

#ifndef HIMRAKSHAK_SIMULATE_SENSOR_FAULT
#define HIMRAKSHAK_SIMULATE_SENSOR_FAULT 0
#endif

#include "gts_root_r4.h"
#include "station_config.h"
#include "sensor_config.h"
#include "simulation_scenarios.h"
#include "sensor_manager.h"
#include "HimRakshakModel.h"
#include "tinyml_feature_contract.h"

// -----------------------------------------------------------------------------
// Network and controller configuration (existing V2 transport contract).
// -----------------------------------------------------------------------------
static const char *WIFI_SSID = "Wokwi-GUEST";
static const char *WIFI_PASSWORD = "";
static const char *TLS_HOST = "himrakshak.onrender.com";
static const char *INGEST_URL = "https://himrakshak.onrender.com/api/stations/";

static const uint32_t TELEMETRY_INTERVAL_MS = 10000UL;
static const uint32_t DIAGNOSTIC_INTERVAL_MS = 1000UL;
static const uint32_t FEATURE_LOG_INTERVAL_MS = 5000UL;
static const uint32_t WIFI_TIMEOUT_MS = 15000UL;
static const uint32_t NTP_TIMEOUT_MS = 15000UL;
static const uint32_t NETWORK_SYNC_RETRY_MS = 30000UL;
static const uint32_t RETRY_BASE_MS = 5000UL;
static const uint32_t RETRY_MAX_MS = 60000UL;
static const size_t MAX_PENDING_PACKETS = 10U;
static const uint32_t REMOTE_COMMAND_POLL_MS = 3000UL;
static const uint32_t REMOTE_COMMAND_ACK_RETRY_MS = 5000UL;
static const char *DEVICE_ID = "HR-01-WOKWI";
#ifndef HIMRAKSHAK_REMOTE_CONTROL_ENABLED
#define HIMRAKSHAK_REMOTE_CONTROL_ENABLED 1
#endif
#ifndef HIMRAKSHAK_LOG_LEVEL
#define HIMRAKSHAK_LOG_LEVEL 1
#endif

static const uint8_t GREEN_LED = HimRakshakConfig::Pins::kGreenLed;
static const uint8_t YELLOW_LED = HimRakshakConfig::Pins::kYellowLed;
static const uint8_t RED_LED = HimRakshakConfig::Pins::kRedLed;
static const uint8_t BUZZER_PIN = HimRakshakConfig::Pins::kBuzzer;

struct Assessment {
  int score;
  const char *level;
  int tinymlPrediction;
  bool tinymlReady;
};

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

using HimRakshakSensors::SensorFrame;

ControllerState controllerState = BOOT;
ControllerState previousState = BOOT;
CommunicationState communicationState = COMMUNICATION_UNKNOWN;
uint32_t lastDiagnosticLog = 0;
uint32_t lastFeatureLog = 0;
uint32_t lastReport = 0;
uint32_t recoveryUntil = 0;
uint32_t lastBeep = 0;
bool sensorReadHealthy = false;

// -----------------------------------------------------------------------------
// Sensor manager and simulation scenario source.
// -----------------------------------------------------------------------------
HimRakshakSensors::SensorManager sensorManager;
HimRakshakSimulation::ScenarioManager scenarioManager;
HimRakshakSimulation::Scenario activeScenario =
    HimRakshakSimulation::Scenario::kLiveHardware;
bool sensorTaskStarted = false;
TaskHandle_t sensorTaskHandle = nullptr;

// The acquisition task owns SensorManager. loop() reads only this snapshot.
// Copying the small POD-like SensorFrame under a portMUX prevents torn reads.
static portMUX_TYPE sensorFrameMux = portMUX_INITIALIZER_UNLOCKED;
SensorFrame latestSensorFrame = {};
volatile bool latestSensorFrameAvailable = false;

struct RemoteCommandSnapshot {
  bool valid = false;
  bool custom = false;
  uint32_t version = 0U;
  HimRakshakSimulation::Scenario scenario = HimRakshakSimulation::Scenario::kNormal;
  HimRakshakSimulation::RemoteSensorValues values;
};
static portMUX_TYPE remoteCommandMux = portMUX_INITIALIZER_UNLOCKED;
RemoteCommandSnapshot pendingRemoteCommand;
volatile uint32_t remoteCommandAppliedVersion = 0U;
uint32_t remoteCommandReceivedVersion = 0U;
uint32_t remoteCommandAckedVersion = 0U;
uint32_t lastRemoteCommandPollAt = 0U;
uint32_t lastRemoteCommandAckAt = 0U;
int lastCompactPrediction = -999;
bool lastCompactModelReady = false;

void publishSensorFrame(const SensorFrame &frame) {
  portENTER_CRITICAL(&sensorFrameMux);
  latestSensorFrame = frame;
  latestSensorFrameAvailable = true;
  portEXIT_CRITICAL(&sensorFrameMux);
}

bool copyLatestSensorFrame(SensorFrame &destination) {
  bool available = false;
  portENTER_CRITICAL(&sensorFrameMux);
  available = latestSensorFrameAvailable;
  if (available) destination = latestSensorFrame;
  portEXIT_CRITICAL(&sensorFrameMux);
  return available;
}

void publishRemoteCommand(const RemoteCommandSnapshot &command) {
  portENTER_CRITICAL(&remoteCommandMux);
  pendingRemoteCommand = command;
  portEXIT_CRITICAL(&remoteCommandMux);
}

bool copyRemoteCommand(RemoteCommandSnapshot &destination) {
  portENTER_CRITICAL(&remoteCommandMux);
  destination = pendingRemoteCommand;
  portEXIT_CRITICAL(&remoteCommandMux);
  return destination.valid;
}

uint32_t getRemoteCommandAppliedVersion() {
  uint32_t version;
  portENTER_CRITICAL(&remoteCommandMux);
  version = remoteCommandAppliedVersion;
  portEXIT_CRITICAL(&remoteCommandMux);
  return version;
}

void sensorAcquisitionTask(void *parameter) {
  (void)parameter;
  uint32_t appliedVersion = 0U;
  for (;;) {
    RemoteCommandSnapshot command;
    if (copyRemoteCommand(command) && command.version > appliedVersion) {
      if (command.custom) scenarioManager.setRemoteValues(command.values);
      else scenarioManager.clearRemoteValues();
      scenarioManager.setScenario(command.custom
          ? HimRakshakSimulation::Scenario::kRemoteCustom
          : command.scenario, millis());
      activeScenario = command.custom
          ? HimRakshakSimulation::Scenario::kRemoteCustom : command.scenario;
      appliedVersion = command.version;
      portENTER_CRITICAL(&remoteCommandMux);
      remoteCommandAppliedVersion = appliedVersion;
      portEXIT_CRITICAL(&remoteCommandMux);
    }
    scenarioManager.update(millis());
    sensorManager.setSimulationStimulus(scenarioManager.stimulus());
    sensorManager.update();
    publishSensorFrame(sensorManager.frame());
    // 1 ms task yield; scenario MPU sampling uses the 20 ms (50 Hz) cadence.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void updateSensorsInlineFallback() {
  // Used only if the ESP32 cannot create the dedicated acquisition task.
  static uint32_t appliedVersion = 0U;
  RemoteCommandSnapshot command;
  if (copyRemoteCommand(command) && command.version > appliedVersion) {
    if (command.custom) scenarioManager.setRemoteValues(command.values);
    else scenarioManager.clearRemoteValues();
    scenarioManager.setScenario(command.custom
        ? HimRakshakSimulation::Scenario::kRemoteCustom : command.scenario, millis());
    activeScenario = command.custom
        ? HimRakshakSimulation::Scenario::kRemoteCustom : command.scenario;
    appliedVersion = command.version;
    portENTER_CRITICAL(&remoteCommandMux);
    remoteCommandAppliedVersion = appliedVersion;
    portEXIT_CRITICAL(&remoteCommandMux);
  }
  scenarioManager.update(millis());
  sensorManager.setSimulationStimulus(scenarioManager.stimulus());
  sensorManager.update();
  publishSensorFrame(sensorManager.frame());
}

// -----------------------------------------------------------------------------
// Existing bounded queue, sequence synchronization and verified HTTPS state.
// -----------------------------------------------------------------------------
PendingPacket pending[MAX_PENDING_PACKETS];
size_t pendingHead = 0;
size_t pendingCount = 0;
unsigned long sequenceNumber = 0;
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

bool wifiAttemptInProgress = false;
unsigned long wifiAttemptStartedAt = 0;
bool ntpConfigurationStarted = false;
bool timeSynchronized = false;
unsigned long ntpStartedAt = 0;
bool startupSequenceSynchronized = false;
unsigned long lastStartupSyncAttempt = 0;

const char *stateName(ControllerState state) {
  switch (state) {
    case BOOT: return "BOOT";
    case INITIALIZING: return "INITIALIZING";
    case NORMAL: return "NORMAL";
    case WARNING: return "WARNING";
    case CRITICAL: return "CRITICAL";
    case SENSOR_FAULT: return "SENSOR_FAULT";
    case RECOVERY: return "RECOVERY";
    default: return "UNKNOWN";
  }
}

const char *communicationStateName(CommunicationState state) {
  switch (state) {
    case COMMUNICATION_UNKNOWN: return "COMMUNICATION_UNKNOWN";
    case COMMUNICATION_CONNECTED: return "COMMUNICATION_CONNECTED";
    case COMMUNICATION_DEGRADED: return "COMMUNICATION_DEGRADED";
    default: return "COMMUNICATION_UNKNOWN";
  }
}

// -----------------------------------------------------------------------------
// Risk fusion: rule-based safety checks remain independent from TinyML.
// ML may raise the final severity, but it never downgrades a safety rule.
// -----------------------------------------------------------------------------
Assessment assessRisk(const SensorFrame &frame,
                      int tinymlPrediction,
                      bool tinymlReady) {
  const float rainfallHistoryContribution = frame.rainfallHistoryValid
      ? constrain(frame.rainfall24hMm / 220.0f, 0.0f, 1.0f) * 16.0f
      : 0.0f;

  const float scoreRaw =
      rainfallHistoryContribution +
      constrain(frame.subsurfaceMoisturePct / 100.0f, 0.0f, 1.0f) * 12.0f +
      constrain(frame.porePressureKpa / 160.0f, 0.0f, 1.0f) * 18.0f +
      constrain(fabsf(frame.tiltXDeg) / 15.0f, 0.0f, 1.0f) * 18.0f +
      constrain(frame.groundDisplacementRateMmPerHour / 8.0f, 0.0f, 1.0f) * 18.0f +
      constrain(frame.vibrationPeakG / 3.5f, 0.0f, 1.0f) * 8.0f +
      (1.0f - constrain(frame.groundwaterDepthM / 10.0f, 0.0f, 1.0f)) * 10.0f;
  const int score = static_cast<int>(constrain(lroundf(scoreRaw), 0L, 100L));

  const bool criticalRainRule = frame.rainfallHistoryValid &&
      frame.rainfall24hMm >= 145.0f && frame.moisture >= 72 &&
      frame.porePressureKpa >= 82.0f;
  const bool criticalRule = frame.tiltXDeg >= 8.5f ||
      frame.groundDisplacementRateMmPerHour >= 2.2f ||
      (frame.porePressureKpa >= 108.0f && frame.subsurfaceMoisturePct >= 72.0f) ||
      criticalRainRule ||
      (frame.tiltRateDegPerHour >= 1.8f && frame.porePressureKpa >= 78.0f);

  const bool warningRainRule = frame.rainfallHistoryValid &&
      frame.rainfall24hMm >= 65.0f && frame.moisture >= 54;
  const bool warningRule = warningRainRule ||
      frame.porePressureKpa >= 66.0f || frame.tiltXDeg >= 4.1f ||
      frame.groundDisplacementRateMmPerHour >= 0.55f ||
      frame.vibrationPeakG >= 0.95f || frame.groundwaterDepthM <= 1.8f ||
      frame.rainfallIntensityMmH >= 35.0f;

  const int ruleClass = criticalRule ? 2 : (warningRule ? 1 : 0);
  const int mlClass = (tinymlReady && tinymlPrediction >= 0)
      ? tinymlPrediction : 0;
  const int finalClass = max(ruleClass, mlClass);
  const char *level = finalClass == 2 ? "CRITICAL" :
                      (finalClass == 1 ? "WARNING" : "NORMAL");

  return {score, level, tinymlPrediction, tinymlReady};
}

ControllerState evaluateState(const SensorFrame &frame,
                              const Assessment &assessment) {
  // Incomplete rainfall history is a MODEL WARM-UP condition, not a hardware
  // sensor fault. Rule-based local protection continues during this period.
  if (!frame.healthy || !frame.groundwaterValid) {
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
  if (controllerState == RECOVERY &&
      static_cast<int32_t>(millis() - recoveryUntil) < 0) return RECOVERY;
  return NORMAL;
}

void transitionState(ControllerState next) {
  if (next == controllerState) return;
  previousState = controllerState;
  controllerState = next;
  if (next == RECOVERY) recoveryUntil = millis() + 1500UL;
  Serial.printf("[STATE TRANSITION] %s -> %s\n",
                stateName(previousState), stateName(controllerState));
}

void reportDecision(const SensorFrame &frame,
                    const Assessment &assessment,
                    const char *modelInputStatus) {
  Serial.printf("[EDGE DECISION] score=%d final=%s ml=%s(%d) inputs=%s\n",
                assessment.score, assessment.level,
                HimRakshakTinyMLContract::className(assessment.tinymlPrediction),
                assessment.tinymlPrediction, modelInputStatus);
  Serial.printf("[RAINFALL] intensity=%.2fmm/h oneHour=%.2fmm twentyFourHour=%.2fmm history=%s coverage=%u/1440 simulated_min\n",
                frame.rainfallIntensityMmH, frame.rainfall1hMm,
                frame.rainfall24hMm, frame.rainfallHistoryValid ? "VALID" : "WARMING",
                static_cast<unsigned int>(frame.rainfallHistoryCoverageMinutes));
  Serial.printf("[SOIL] surface=%d%% subsurface=%.2f%% pore=%.2fkPa soilTemp=%.2fC\n",
                frame.moisture, frame.subsurfaceMoisturePct,
                frame.porePressureKpa, frame.soilTemperatureC);
  Serial.printf("[MOVEMENT] tiltX=%.3fdeg tiltY=%.3fdeg tiltRate=%.3fdeg/h displacement=%.3fmm displacementRate=%.3fmm/h\n",
                frame.tiltXDeg, frame.tiltYDeg, frame.tiltRateDegPerHour,
                frame.groundDisplacementMm, frame.groundDisplacementRateMmPerHour);
  Serial.printf("[VIBRATION] rms=%.4fg peak=%.4fg dominant=%.2fHz valid=%s\n",
                frame.vibrationRmsG, frame.vibrationPeakG,
                frame.vibrationDominantHz, frame.vibrationValid ? "YES" : "NO");
  Serial.printf("[CONTEXT] groundwater=%.3fm airTemp=%.2fC humidity=%.2f%% pressure=%.2fhPa battery=%d%% sourceHealth=%s\n",
                frame.groundwaterDepthM, frame.airTemperatureC,
                frame.relativeHumidityPct, frame.atmosphericPressureHpa,
                frame.battery, frame.healthy ? "OK" : "FAULT");
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
  packet += String(fabsf(frame.tiltXDeg), 2);
  packet += ",\"vibration_level\":";
  packet += frame.vibration;
  packet += ",\"battery_pct\":";
  packet += frame.battery;
  packet += ",\"sensor_schema_version\":2";
  packet += ",\"rainfall_intensity_mm_h\":" + String(frame.rainfallIntensityMmH, 2);
  packet += ",\"rainfall_1h_mm\":" + String(frame.rainfall1hMm, 2);
  packet += ",\"rainfall_24h_mm\":" + String(frame.rainfall24hMm, 2);
  packet += ",\"soil_moisture_surface_pct\":" + String(static_cast<float>(frame.moisture), 2);
  packet += ",\"soil_moisture_subsurface_pct\":" + String(frame.subsurfaceMoisturePct, 2);
  packet += ",\"pore_water_pressure_kpa\":" + String(frame.porePressureKpa, 2);
  packet += ",\"tilt_x_deg\":" + String(frame.tiltXDeg, 3);
  packet += ",\"tilt_y_deg\":" + String(frame.tiltYDeg, 3);
  packet += ",\"tilt_rate_deg_per_h\":" + String(frame.tiltRateDegPerHour, 3);
  packet += ",\"ground_displacement_mm\":" + String(frame.groundDisplacementMm, 3);
  packet += ",\"ground_displacement_rate_mm_per_h\":" + String(frame.groundDisplacementRateMmPerHour, 3);
  packet += ",\"vibration_rms_g\":" + String(frame.vibrationRmsG, 4);
  packet += ",\"vibration_peak_g\":" + String(frame.vibrationPeakG, 4);
  packet += ",\"vibration_dominant_hz\":" + String(frame.vibrationDominantHz, 2);
  packet += ",\"soil_temperature_c\":" + String(frame.soilTemperatureC, 2);
  packet += ",\"groundwater_depth_m\":" + String(frame.groundwaterDepthM, 3);
  packet += ",\"air_temperature_c\":" + String(frame.airTemperatureC, 2);
  packet += ",\"relative_humidity_pct\":" + String(frame.relativeHumidityPct, 2);
  packet += ",\"atmospheric_pressure_hpa\":" + String(frame.atmosphericPressureHpa, 2);
  packet += ",\"tinyml_prediction\":" + String(assessment.tinymlPrediction);
  packet += ",\"model_ready\":" + String(assessment.tinymlReady ? "true" : "false");
  packet += ",\"feature_inputs_valid\":" + String(frame.healthy ? "true" : "false");
  packet += ",\"rainfall_history_valid\":" + String(frame.rainfallHistoryValid ? "true" : "false");
  packet += ",\"groundwater_valid\":" + String(frame.groundwaterValid ? "true" : "false");
  packet += ",\"dht22_valid\":" + String(frame.dht22Valid ? "true" : "false");
  packet += ",\"bmp280_valid\":" + String(frame.bmp280Valid ? "true" : "false");
  packet += ",\"model_id\":\"";
  packet += HimRakshakTinyML::MODEL_ID;
  packet += "\",\"model_sha256\":\"";
  packet += HimRakshakTinyML::MODEL_SHA256;
  packet += "\",\"measurement_provenance\":\"";
  packet += (activeScenario == HimRakshakSimulation::Scenario::kLiveHardware)
      ? "WOKWI_VIRTUAL_COMPONENTS" : "WOKWI_SYNTHETIC_SCENARIO_STIMULUS";
  packet += "\",\"scenario\":\"";
  packet += HimRakshakSimulation::ScenarioManager::nameOf(activeScenario);
  packet += "\",\"rainfall_history_mode\":\"ROLLING_FROM_BOOT_ACCELERATED_60X\"";
  packet += ",\"risk\":\"";
  packet += assessment.level;
  packet += "\",\"risk_score_demo\":";
  packet += assessment.score;
  packet += ",\"sensor_health\":\"";
  packet += frame.healthy ? "OK" : "FAULT";
  packet += "\",\"transport\":\"WOKWI_SOFTWARE_SIMULATION\",\"is_simulated\":true}";
  return packet;
}


// -----------------------------------------------------------------------------
// Local diagnostics and JSON payload creation.
// -----------------------------------------------------------------------------
void reportStatus(const char *message) {
#if HIMRAKSHAK_LOG_LEVEL >= 2
  Serial.printf("[TELEMETRY DEBUG] %s | station=%s pending=%u attempted=%lu delivered=%lu failed=%lu retries=%lu last_error=%s\n",
                message, HIMRAKSHAK_STATION_ID, (unsigned)pendingCount,
                packetsAttempted, packetsDelivered, failedRequests, retryCount,
                lastError.length() ? lastError.c_str() : "none");
#else
  Serial.printf("[TELEMETRY] %s\n", message);
#endif
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
    // Do not mark the full communication path CONNECTED here. NTP and backend
    // sequence synchronization are still required; reportCommunicationState()
    // is the single authority for the combined state.
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

bool jsonFloatValue(const String &json, const char *key, float &output) {
  const String value = jsonValue(json, key);
  if (value.length() == 0) return false;
  char *end = nullptr;
  const float parsed = strtof(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0' || !isfinite(parsed)) return false;
  output = parsed;
  return true;
}

bool parseRemoteCommand(const String &json, RemoteCommandSnapshot &command) {
  const String versionText = jsonValue(json, "version");
  unsigned long version = 0UL;
  if (!parseUnsigned(versionText, version) || version == 0UL) return false;
  const String mode = jsonValue(json, "mode");
  const String scenarioName = jsonValue(json, "scenario");
  command = RemoteCommandSnapshot();
  command.valid = true;
  command.version = static_cast<uint32_t>(version);
  command.custom = (mode == "custom");
  command.scenario = command.custom
      ? HimRakshakSimulation::Scenario::kRemoteCustom
      : HimRakshakSimulation::ScenarioManager::fromName(scenarioName.c_str());
  if (command.scenario == HimRakshakSimulation::Scenario::kLiveHardware) return false;
  if (!command.custom) return true;

  command.values.active = true;
  return jsonFloatValue(json, "rainfall_intensity_mm_h", command.values.rainfallIntensityMmH) &&
      jsonFloatValue(json, "surface_moisture_pct", command.values.surfaceMoisturePct) &&
      jsonFloatValue(json, "subsurface_moisture_pct", command.values.subsurfaceMoisturePct) &&
      jsonFloatValue(json, "pore_pressure_kpa", command.values.porePressureKpa) &&
      jsonFloatValue(json, "tilt_x_deg", command.values.tiltXDeg) &&
      jsonFloatValue(json, "tilt_y_deg", command.values.tiltYDeg) &&
      jsonFloatValue(json, "ground_displacement_mm", command.values.groundDisplacementMm) &&
      jsonFloatValue(json, "vibration_rms_g", command.values.vibrationRmsG) &&
      jsonFloatValue(json, "vibration_frequency_hz", command.values.vibrationFrequencyHz) &&
      jsonFloatValue(json, "soil_temperature_c", command.values.soilTemperatureC) &&
      jsonFloatValue(json, "groundwater_depth_m", command.values.groundwaterDepthM) &&
      jsonFloatValue(json, "air_temperature_c", command.values.airTemperatureC) &&
      jsonFloatValue(json, "relative_humidity_pct", command.values.relativeHumidityPct) &&
      jsonFloatValue(json, "atmospheric_pressure_hpa", command.values.atmosphericPressureHpa);
}

bool postRemoteCommandAck(uint32_t version) {
  if (version == 0U || WiFi.status() != WL_CONNECTED || !timeSynchronized) return false;
  WiFiClientSecure client;
  client.setCACert(HIMRAKSHAK_ROOT_CA);
  client.setTimeout(5000);
  HTTPClient https;
  const String url = String(INGEST_URL) + HIMRAKSHAK_STATION_ID + "/simulation-command/ack";
  if (!https.begin(client, url)) return false;
  https.setConnectTimeout(5000);
  https.setTimeout(5000);
  https.addHeader("Content-Type", "application/json");
  String body = String("{\"version\":") + version + ",\"device_id\":\"" + DEVICE_ID + "\"}";
  const int status = https.POST(body);
  https.end();
  if (status >= 200 && status < 300) {
    remoteCommandAckedVersion = version;
    Serial.printf("[REMOTE] command %lu applied and acknowledged\n", static_cast<unsigned long>(version));
    return true;
  }
  return false;
}

void serviceRemoteCommands() {
#if HIMRAKSHAK_REMOTE_CONTROL_ENABLED
  if (WiFi.status() != WL_CONNECTED || !timeSynchronized || !startupSequenceSynchronized) return;
  const uint32_t now = millis();
  const uint32_t applied = getRemoteCommandAppliedVersion();
  if (applied > remoteCommandAckedVersion &&
      (lastRemoteCommandAckAt == 0U || now - lastRemoteCommandAckAt >= REMOTE_COMMAND_ACK_RETRY_MS)) {
    lastRemoteCommandAckAt = now;
    postRemoteCommandAck(applied);
  }
  if (lastRemoteCommandPollAt != 0U && now - lastRemoteCommandPollAt < REMOTE_COMMAND_POLL_MS) return;
  lastRemoteCommandPollAt = now;
  WiFiClientSecure client;
  client.setCACert(HIMRAKSHAK_ROOT_CA);
  client.setTimeout(5000);
  HTTPClient https;
  String url = String(INGEST_URL) + HIMRAKSHAK_STATION_ID + "/simulation-command?device_id=" + DEVICE_ID;
  if (!https.begin(client, url)) return;
  https.setConnectTimeout(5000);
  https.setTimeout(5000);
  const int status = https.GET();
  const String response = status > 0 ? https.getString() : String();
  https.end();
  if (status != 200) return;
  RemoteCommandSnapshot command;
  if (!parseRemoteCommand(response, command)) return;
  if (command.version <= remoteCommandReceivedVersion) return;
  publishRemoteCommand(command);
  remoteCommandReceivedVersion = command.version;
  Serial.printf("[REMOTE] command %lu received (%s / %s)\n",
      static_cast<unsigned long>(command.version),
      command.custom ? "custom" : "scenario",
      HimRakshakSimulation::ScenarioManager::nameOf(command.scenario));
#endif
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
#if HIMRAKSHAK_LOG_LEVEL >= 2
  Serial.printf("[HTTPS DEBUG] POST sequence=%lu payload=%s\n", packet.sequence, packet.json.c_str());
#endif
  int status = https.POST(packet.json);
  String response = https.getString();
  https.end();
  lastResponse = response;
#if HIMRAKSHAK_LOG_LEVEL >= 2
  Serial.printf("[HTTPS DEBUG] status=%d response=%s\n", status, response.c_str());
#endif
  if (status >= 200 && status < 300) {
    packetsDelivered++;
    Serial.printf("[TELEMETRY] sequence %lu delivered\n", packet.sequence);
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
    packet.retryDelay = packet.retryDelay > (RETRY_MAX_MS / 2UL)
        ? RETRY_MAX_MS : packet.retryDelay * 2UL;
  } else {
    retryCount++;
    packet.retryDelay = packet.retryDelay > (RETRY_MAX_MS / 2UL)
        ? RETRY_MAX_MS : packet.retryDelay * 2UL;
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
  Serial.begin(HimRakshakConfig::Timing::kSerialBaud);
  delay(50);
  Serial.println();
  Serial.println(F("[BOOT] HimRakshak V2 edge-AI sender"));
  controllerState = INITIALIZING;

  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);
  noTone(BUZZER_PIN);

  // Select the scenario only when explicitly enabled in station_config.h.
  // Otherwise use the actual Wokwi component inputs.
#if HIMRAKSHAK_SIMULATE_SENSOR_FAULT
  activeScenario = HimRakshakSimulation::Scenario::kSensorFailure;
#elif HIMRAKSHAK_USE_SCENARIO_PRESET
  activeScenario = HimRakshakSimulation::ScenarioManager::fromName(HIMRAKSHAK_SCENARIO);
#else
  activeScenario = HimRakshakSimulation::Scenario::kLiveHardware;
#endif

  scenarioManager.begin(activeScenario, millis());
  sensorManager.setSimulationStimulus(scenarioManager.stimulus());
  if (!sensorManager.begin()) {
    Serial.println(F("[FATAL] SensorManager initialization failed"));
  }
#if HIMRAKSHAK_LOG_LEVEL >= 2
  sensorManager.printDiagnostics();
#endif
  publishSensorFrame(sensorManager.frame());

  Serial.printf("[STATION] id=%s scenario=%s mode=%s\n",
                HIMRAKSHAK_STATION_ID,
                HimRakshakSimulation::ScenarioManager::nameOf(activeScenario),
                scenarioManager.enabled() ? "SCENARIO_STIMULUS" : "LIVE_COMPONENTS");
  Serial.println(F("[NETWORK] HTTPS telemetry + remote command polling enabled"));
#if HIMRAKSHAK_LOG_LEVEL >= 2
  Serial.printf("[NETWORK DEBUG] bounded_queue=%u retry=%lu..%lums\n",
                static_cast<unsigned>(MAX_PENDING_PACKETS), RETRY_BASE_MS, RETRY_MAX_MS);
  HimRakshakTinyMLContract::printModelInfo(Serial);
  Serial.println(F("[ML DATA PROVENANCE] synthetic demonstration training data; not field-validated"));
#endif

  // Sampling runs in its own high-priority task so loop() HTTPS operations do
  // not interrupt acquisition. Live mode targets 100 Hz; scenario mode 50 Hz.
  const BaseType_t taskResult = xTaskCreatePinnedToCore(
      sensorAcquisitionTask,
      "hr_sensor_task",
      8192,
      nullptr,
      3,
      &sensorTaskHandle,
      1);
  sensorTaskStarted = (taskResult == pdPASS);
  if (!sensorTaskStarted) {
    Serial.println(F("[WARNING] Sensor task creation failed; using loop-based acquisition fallback"));
  } else {
    Serial.printf("[SENSOR TASK] started on ESP32 core 1; %u Hz MPU sampling target (%s)\n",
                  scenarioManager.enabled() ? 50U : 100U,
                  scenarioManager.enabled() ? "scenario mode" : "live mode");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  connectWiFi();  // Non-blocking initial connection attempt.
}

void loop() {
  if (!sensorTaskStarted) {
    updateSensorsInlineFallback();
  }

  SensorFrame frame = {};
  if (!copyLatestSensorFrame(frame)) {
    frame = sensorManager.frame();
  }
  sensorReadHealthy = frame.healthy;

  HimRakshakTinyMLContract::FeatureVector modelInput;
  const bool modelReady =
      HimRakshakTinyMLContract::buildFeatureVector(frame, modelInput);
  const int tinymlPrediction = modelReady
      ? HimRakshakTinyMLContract::predict(modelInput)
      : HimRakshakTinyMLContract::kInvalidPrediction;

  Assessment assessment = assessRisk(frame, tinymlPrediction, modelReady);
  transitionState(evaluateState(frame, assessment));
  updateIndicators();

  const uint32_t now = millis();
  if (now - lastDiagnosticLog >= DIAGNOSTIC_INTERVAL_MS) {
    lastDiagnosticLog = now;
    if (modelReady != lastCompactModelReady || tinymlPrediction != lastCompactPrediction) {
      lastCompactModelReady = modelReady;
      lastCompactPrediction = tinymlPrediction;
      Serial.printf("[AI] ready=%s prediction=%s(%d) final=%s score=%d\n",
          modelReady ? "YES" : "NO",
          HimRakshakTinyMLContract::className(tinymlPrediction),
          tinymlPrediction, assessment.level, assessment.score);
    }
#if HIMRAKSHAK_LOG_LEVEL >= 2
    if (!frame.rainfallHistoryValid) {
      Serial.printf("[MODEL WARM-UP] rainfall history coverage=%u/1440 simulated minutes\n",
                    static_cast<unsigned int>(frame.rainfallHistoryCoverageMinutes));
    }
    if (!frame.healthy) Serial.println(F("[SENSOR HEALTH] required source invalid; inference inhibited"));
    reportDecision(frame, assessment, modelReady ? "VALID_16_FEATURES" :
                   (modelInput.error != nullptr ? modelInput.error : "NOT_READY"));
#endif
  }

#if HIMRAKSHAK_LOG_LEVEL >= 2
  if (modelReady && now - lastFeatureLog >= FEATURE_LOG_INTERVAL_MS) {
    lastFeatureLog = now;
    HimRakshakTinyMLContract::printFeatureVector(Serial, modelInput);
  }
#endif

  if (now - lastReport >= TELEMETRY_INTERVAL_MS) {
    lastReport = now;
    const String packet = buildPacket(sequenceNumber, frame, assessment);
    Serial.printf("[EDGE] state=%s local-outputs=updated\n", stateName(controllerState));
#if HIMRAKSHAK_LOG_LEVEL >= 2
    Serial.printf("[TELEMETRY DEBUG] sequence=%lu battery=%d risk=%s score=%d tinyml=%d ready=%s\n",
                  sequenceNumber, frame.battery, assessment.level,
                  assessment.score, assessment.tinymlPrediction,
                  assessment.tinymlReady ? "YES" : "NO");
#endif
    if (enqueuePacket(packet, sequenceNumber)) {
      sequenceNumber++;
      reportStatus("V2 sensor frame queued for certificate-verified HTTPS delivery");
    }
  }

  // Network activity is confined to loop(); the independent sensor task keeps
  // acquiring samples during potentially slow DNS/TLS/HTTP operations.
  serviceNetwork();
  serviceRemoteCommands();
  delay(2);
}
