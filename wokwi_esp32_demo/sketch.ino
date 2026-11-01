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
#include <math.h>

#include "isrg_root_x1.h"
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
const char *INGEST_URL = "https://himrakshak.onrender.com/api/stations/";
const unsigned long TELEMETRY_INTERVAL_MS = 10000;
const unsigned long WIFI_TIMEOUT_MS = 15000;
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
unsigned long nextWiFiAttempt = 0;
String lastError = "";
String lastDelivery = "never";

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
  Serial.printf("[%lu] %s | station=%s pending=%u attempted=%lu delivered=%lu failed=%lu retries=%lu last_error=%s\n",
                millis(), message, HIMRAKSHAK_STATION_ID, (unsigned)pendingCount,
                packetsAttempted, packetsDelivered, failedRequests, retryCount,
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

int postPacket(PendingPacket &packet) {
  packetsAttempted++;
  if (!connectWiFi()) return -1;
  WiFiClientSecure client;
  client.setCACert(HIMRAKSHAK_ROOT_CA);
  client.setTimeout(8000);
  HTTPClient https;
  String url = String(INGEST_URL) + HIMRAKSHAK_STATION_ID + "/readings";
  if (!https.begin(client, url)) {
    lastError = "HTTPS begin failed";
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
    lastError = String("HTTP ") + status;
  } else {
    lastError = "HTTPS request failed or timed out";
  }
  return status;
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
    // The backend is authoritative. Keep the packet until a later manual
    // review/restart rather than silently discarding an ambiguous response.
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
