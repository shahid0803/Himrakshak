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

const int MOISTURE_PIN = 34;
const int TILT_PIN = 35;
const int BATTERY_PIN = 32;
const int VIBRATION_PIN = 18;
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
  int moisture;
  float tiltDeg;
  int vibration;
  int battery;
  bool healthy;
};

ControllerState controllerState = BOOT;
ControllerState previousState = BOOT;
CommunicationState communicationState = COMMUNICATION_UNKNOWN;
SensorFrame filteredFrame = {0, 0.0f, 0, 0, false};
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

Assessment assessRisk(int moisture, float tiltDeg, int vibration) {
  float moisturePart = max(0.0f, (moisture - 45.0f) / 55.0f) * 35.0f;
  float tiltPart = max(0.0f, (tiltDeg - 2.0f) / 10.0f) * 45.0f;
  float vibrationPart = max(0.0f, (vibration - 25.0f) / 75.0f) * 20.0f;
  int score = (int)constrain(round(moisturePart + tiltPart + vibrationPart), 0, 100);
  const char *level = "NORMAL";
  if (tiltDeg >= 8.0f || (moisture >= 75 && tiltDeg >= 4.0f) || score >= 55) {
    level = "CRITICAL";
  } else if (moisture >= 60 || tiltDeg >= 4.0f || vibration >= 65 || score >= 25) {
    level = "WARNING";
  }
  return {score, level};
}

bool validAdc(int value) {
  // ESP32 analogRead() is expected to return a 12-bit value.
  // The full 0..4095 range is valid for a potentiometer at either endpoint.
  return value >= ADC_MIN_VALID && value <= ADC_MAX_VALID;
}

void readSensors(int &moisture, float &tiltDeg, int &vibration, int &battery) {
  int moistureRaw = analogRead(MOISTURE_PIN);
  int tiltRaw = analogRead(TILT_PIN);
  int batteryRaw = analogRead(BATTERY_PIN);

  bool healthy = validAdc(moistureRaw) &&
                 validAdc(tiltRaw) &&
                 validAdc(batteryRaw);

#if HIMRAKSHAK_SIMULATE_SENSOR_FAULT
  healthy = false;
#endif

  moisture = map(constrain(moistureRaw, 0, 4095), 0, 4095, 0, 100);
  tiltDeg = map(constrain(tiltRaw, 0, 4095), 0, 4095, 0, 150) / 10.0f;
  battery = map(constrain(batteryRaw, 0, 4095), 0, 4095, 0, 100);

  // The pushbutton represents a discrete vibration event. Keep this input
  // responsive instead of smoothing away a short button press.
  vibration = (digitalRead(VIBRATION_PIN) == LOW) ? 85 : 12;

#if HIMRAKSHAK_USE_SCENARIO_PRESET
  if (strcmp(HIMRAKSHAK_SCENARIO, "heavy_rain") == 0) {
    moisture = max(moisture, 78);
    tiltDeg = max(tiltDeg, 4.5f);
    vibration = max(vibration, 30);
  } else if (strcmp(HIMRAKSHAK_SCENARIO, "slope_shift") == 0) {
    moisture = max(moisture, 70);
    tiltDeg = max(tiltDeg, 9.0f);
    vibration = max(vibration, 55);
  } else if (strcmp(HIMRAKSHAK_SCENARIO, "vibration_burst") == 0) {
    vibration = max(vibration, 88);
  }
#endif

  sensorReadHealthy = healthy;
}

SensorFrame acquireFilteredSensors() {
  unsigned long now = millis();
  if (sensorFilterInitialized &&
      now - lastSensorSample < SENSOR_SAMPLE_INTERVAL_MS) {
    return filteredFrame;
  }

  int moisture;
  float tiltDeg;
  int vibration;
  int battery;
  readSensors(moisture, tiltDeg, vibration, battery);

  if (!sensorFilterInitialized) {
    filteredFrame = {moisture, tiltDeg, vibration, battery, sensorReadHealthy};
    sensorFilterInitialized = true;
  } else {
    filteredFrame.moisture = (int)round(
        FILTER_ALPHA * moisture + (1.0f - FILTER_ALPHA) * filteredFrame.moisture);
    filteredFrame.tiltDeg = FILTER_ALPHA * tiltDeg +
                            (1.0f - FILTER_ALPHA) * filteredFrame.tiltDeg;
    filteredFrame.battery = (int)round(
        FILTER_ALPHA * battery + (1.0f - FILTER_ALPHA) * filteredFrame.battery);
    // Preserve the discrete vibration event from this sample.
    filteredFrame.vibration = vibration;
    filteredFrame.healthy = sensorReadHealthy;
  }

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
  if (!frame.healthy) {
    // Do not cancel an already active critical alarm because a sensor fails.
    return controllerState == CRITICAL ? CRITICAL : SENSOR_FAULT;
  }

  // Critical entry is immediate. Critical hold is evaluated BEFORE WARNING,
  // so a small decrease cannot prematurely downgrade a critical condition.
  if (strcmp(assessment.level, "CRITICAL") == 0) return CRITICAL;
  if (controllerState == CRITICAL &&
      (frame.moisture >= 70 || frame.tiltDeg >= 7.0f ||
       frame.vibration >= 75 || assessment.score >= 45)) {
    return CRITICAL;
  }

  if (strcmp(assessment.level, "WARNING") == 0) return WARNING;
  if (controllerState == WARNING &&
      (frame.moisture >= 55 || frame.tiltDeg >= 3.5f ||
       frame.vibration >= 55 || assessment.score >= 20)) {
    return WARNING;
  }

  if (controllerState == SENSOR_FAULT) return RECOVERY;
  if (controllerState == RECOVERY && (long)(millis() - recoveryUntil) < 0) {
    return RECOVERY;
  }
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
  Serial.printf("[EDGE PROCESSING] rule_based_score=%d level=%s\n",
                assessment.score, assessment.level);
  Serial.printf("[DECISION ENGINE] reasons=moisture:%d tilt:%.1f vibration:%d battery:%d health:%s\n",
                frame.moisture, frame.tiltDeg, frame.vibration, frame.battery,
                frame.healthy ? "ok" : "fault");
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
  packet += String(frame.tiltDeg, 1);
  packet += ",\"vibration_level\":";
  packet += frame.vibration;
  packet += ",\"battery_pct\":";
  packet += frame.battery;
  packet += ",\"risk\":\"";
  packet += assessment.level;
  packet += "\",\"risk_score_demo\":";
  packet += assessment.score;
  packet += ",\"sensor_health\":\"";
  packet += frame.healthy ? "OK" : "FAULT";
  packet += "\",\"transport\":\"WOKWI_SOFTWARE_SIMULATION\"}";
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

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);
  noTone(BUZZER_PIN);

  Serial.printf("HimRakshak sender %s starting; scenario=%s; interval=%lus\n",
                HIMRAKSHAK_STATION_ID, HIMRAKSHAK_SCENARIO,
                TELEMETRY_INTERVAL_MS / 1000);
  Serial.printf("[COMMUNICATION STATUS] queue_capacity=%u tls=verified\n",
                (unsigned)MAX_PENDING_PACKETS);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Start Wi-Fi without waiting. Local sensor processing begins immediately.
  connectWiFi();
}

void loop() {
  // Always perform local acquisition, decision-making and actuator control
  // before attempting network operations.
  SensorFrame frame = acquireFilteredSensors();
  Assessment assessment = assessRisk(frame.moisture, frame.tiltDeg,
                                      frame.vibration);
  ControllerState nextState = evaluateState(frame, assessment);
  transitionState(nextState);
  updateIndicators();

  unsigned long now = millis();
  if (now - lastDiagnosticLog >= DIAGNOSTIC_INTERVAL_MS) {
    lastDiagnosticLog = now;
    Serial.printf("[SENSOR ACQUISITION] moisture=%d tilt=%.1f vibration=%d battery=%d health=%s\n",
                  frame.moisture, frame.tiltDeg, frame.vibration,
                  frame.battery, frame.healthy ? "OK" : "FAULT");
    reportDecision(frame, assessment);
  }

  if (now - lastReport >= TELEMETRY_INTERVAL_MS) {
    lastReport = now;
    String packet = buildPacket(sequenceNumber, frame, assessment);
    Serial.printf("[AUTONOMOUS RESPONSE] risk_state=%s local_leds_buzzer=active\n",
                  stateName(controllerState));
    Serial.printf("[TELEMETRY DELIVERY] sequence=%lu battery=%d%% risk=%s score=%d\n",
                  sequenceNumber, frame.battery, assessment.level,
                  assessment.score);
    if (enqueuePacket(packet, sequenceNumber)) {
      sequenceNumber++;
      reportStatus("Packet queued for HTTPS delivery");
    }
  }

  serviceNetwork();
  delay(20);
}
