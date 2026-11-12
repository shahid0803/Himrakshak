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
const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long NTP_TIMEOUT_MS = 15000;
const unsigned long RETRY_BASE_MS = 5000;
const unsigned long RETRY_MAX_MS = 60000;
const size_t MAX_PENDING_PACKETS = 10;

struct Assessment {
  int score;
  const char *level;
};

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

void readSensors(int &moisture, float &tiltDeg, int &vibration, int &battery) {
  moisture = map(analogRead(MOISTURE_PIN), 0, 4095, 0, 100);
  tiltDeg = map(analogRead(TILT_PIN), 0, 4095, 0, 150) / 10.0f;
  battery = map(analogRead(BATTERY_PIN), 0, 4095, 0, 100);
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
}

String buildPacket(unsigned long sequence) {
  int moisture;
  float tiltDeg;
  int vibration;
  int battery;
  readSensors(moisture, tiltDeg, vibration, battery);
  Assessment assessment = assessRisk(moisture, tiltDeg, vibration);
  String packet = "{\"node_id\":\"";
  packet += HIMRAKSHAK_STATION_ID;
  packet += "\",\"sequence\":";
  packet += sequence;
  packet += ",\"timestamp\":\"SIMULATED\",\"soil_moisture_pct\":";
  packet += moisture;
  packet += ",\"tilt_deg\":";
  packet += String(tiltDeg, 1);
  packet += ",\"vibration_level\":";
  packet += vibration;
  packet += ",\"battery_pct\":";
  packet += battery;
  packet += ",\"risk\":\"";
  packet += assessment.level;
  packet += "\",\"risk_score_demo\":";
  packet += assessment.score;
  packet += ",\"transport\":\"WOKWI_SOFTWARE_SIMULATION\"}";
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
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.printf("Wi-Fi connecting to %s...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    nextWiFiAttempt = 0;
    Serial.printf("Wi-Fi connected, IP=%s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  lastError = "Wi-Fi connection timeout";
  nextWiFiAttempt = millis() + RETRY_BASE_MS;
  reportStatus("Wi-Fi unavailable; local processing continues");
  return false;
}

bool synchronizeClock() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  unsigned long started = millis();
  while (!getLocalTime(&timeinfo, 1000) &&
         millis() - started < NTP_TIMEOUT_MS) {
    delay(250);
  }
  if (getLocalTime(&timeinfo, 0)) {
    Serial.printf("NTP synchronized: %04d-%02d-%02dT%02d:%02d:%02dZ\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                  timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                  timeinfo.tm_sec);
    return true;
  }
  lastError = "NTP synchronization timeout";
  reportStatus("NTP unavailable; HTTPS will retain certificate verification");
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
  if (!connectWiFi()) return false;
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

void synchronizeStartupSequence() {
  BackendStationState state;
  if (!getStationState(state)) {
    reportStatus("Station state unavailable; preserving local sequence");
    return;
  }
  if (state.hasLatest && sequenceNumber <= state.latestSequence) {
    sequenceNumber = state.latestSequence + 1;
    Serial.printf("Startup sequence synchronized to %lu after backend sequence %lu\n",
                  sequenceNumber, state.latestSequence);
  } else if (state.hasLatest) {
    Serial.printf("Startup sequence %lu is ahead of backend sequence %lu; preserving local value\n",
                  sequenceNumber, state.latestSequence);
  } else {
    Serial.printf("No accepted backend reading; using configured sequence %lu\n",
                  sequenceNumber);
  }
}

int postPacket(PendingPacket &packet) {
  packetsAttempted++;
  if (!connectWiFi()) return -1;
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

void updateIndicators(const Assessment &assessment) {
  bool critical = strcmp(assessment.level, "CRITICAL") == 0;
  bool warning = strcmp(assessment.level, "WARNING") == 0;
  digitalWrite(GREEN_LED, (!critical && !warning) ? HIGH : LOW);
  digitalWrite(YELLOW_LED, warning ? HIGH : LOW);
  digitalWrite(RED_LED, critical ? HIGH : LOW);
  unsigned long now = millis();
  if (critical && now - lastBeep >= 900) {
    tone(BUZZER_PIN, 1800, 180);
    lastBeep = now;
  } else if (!critical) {
    noTone(BUZZER_PIN);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(VIBRATION_PIN, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  analogReadResolution(12);
  Serial.printf("HimRakshak sender %s starting; scenario=%s; interval=%lus\n",
                HIMRAKSHAK_STATION_ID, HIMRAKSHAK_SCENARIO,
                TELEMETRY_INTERVAL_MS / 1000);
  connectWiFi();
  synchronizeClock();
  synchronizeStartupSequence();
}

void loop() {
  int moisture;
  float tiltDeg;
  int vibration;
  int battery;
  readSensors(moisture, tiltDeg, vibration, battery);
  Assessment assessment = assessRisk(moisture, tiltDeg, vibration);
  updateIndicators(assessment);

  unsigned long now = millis();
  if (now - lastReport >= TELEMETRY_INTERVAL_MS) {
    lastReport = now;
    String packet = buildPacket(sequenceNumber);
    Serial.printf("Generated sequence=%lu battery=%d%% risk=%s score=%d\n",
                  sequenceNumber, battery, assessment.level, assessment.score);
    if (enqueuePacket(packet, sequenceNumber)) {
      sequenceNumber++;
      reportStatus("Packet queued for HTTPS delivery");
    }
  }
  if (WiFi.status() != WL_CONNECTED && (long)(now - nextWiFiAttempt) >= 0) {
    connectWiFi();
  }
  flushQueue();
  delay(50);
}
