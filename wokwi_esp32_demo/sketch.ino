/*
  HimRakshak virtual sensor node
  Run in Wokwi with the included diagram.json.
  Potentiometer #1 simulates soil moisture, potentiometer #2 simulates tilt,
  and the pushbutton simulates a vibration burst.

  This is software simulation. It does NOT use a real sensor or LoRa radio.
*/
#include <Arduino.h>

const int MOISTURE_PIN = 34;  // Potentiometer 1: 0..100
const int TILT_PIN = 35;      // Potentiometer 2: 0..15 degrees
const int VIBRATION_PIN = 18; // Button press = vibration burst
const int GREEN_LED = 25;
const int YELLOW_LED = 26;
const int RED_LED = 27;
const int BUZZER_PIN = 23;

unsigned long lastReport = 0;
unsigned long lastBeep = 0;
unsigned long sequenceNumber = 0;

struct Assessment {
  int score;
  const char* level;
};

Assessment assessRisk(int moisture, float tiltDeg, int vibration) {
  float moisturePart = max(0.0f, (moisture - 45.0f) / 55.0f) * 35.0f;
  float tiltPart = max(0.0f, (tiltDeg - 2.0f) / 10.0f) * 45.0f;
  float vibrationPart = max(0.0f, (vibration - 25.0f) / 75.0f) * 20.0f;
  int score = (int)constrain(round(moisturePart + tiltPart + vibrationPart), 0, 100);

  const char* level = "NORMAL";
  if (tiltDeg >= 8.0f || (moisture >= 75 && tiltDeg >= 4.0f) || score >= 55) {
    level = "CRITICAL";
  } else if (moisture >= 60 || tiltDeg >= 4.0f || vibration >= 65 || score >= 25) {
    level = "WARNING";
  }
  return {score, level};
}

void setup() {
  Serial.begin(115200);
  pinMode(VIBRATION_PIN, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  analogReadResolution(12);
  Serial.println("HimRakshak Wokwi simulation starting. Pot1=moisture, Pot2=tilt, button=vibration.");
}

void loop() {
  int moisture = map(analogRead(MOISTURE_PIN), 0, 4095, 0, 100);
  float tiltDeg = map(analogRead(TILT_PIN), 0, 4095, 0, 150) / 10.0f;
  int vibration = (digitalRead(VIBRATION_PIN) == LOW) ? 85 : 12;
  Assessment a = assessRisk(moisture, tiltDeg, vibration);

  bool high = strcmp(a.level, "CRITICAL") == 0;
  bool warning = strcmp(a.level, "WARNING") == 0;
  digitalWrite(GREEN_LED, (!high && !warning) ? HIGH : LOW);
  digitalWrite(YELLOW_LED, warning ? HIGH : LOW);
  digitalWrite(RED_LED, high ? HIGH : LOW);

  unsigned long now = millis();
  if (high && now - lastBeep >= 900) {
    tone(BUZZER_PIN, 1800, 180);
    lastBeep = now;
  } else if (!high) {
    noTone(BUZZER_PIN);
  }

  if (now - lastReport >= 1000) {
    lastReport = now;
    sequenceNumber++;
    // This one-line JSON is easy to copy from Wokwi Serial Monitor into the browser dashboard.
    Serial.printf(
      "{\"node_id\":\"HR-01\",\"sequence\":%lu,\"timestamp\":\"SIMULATED\",\"soil_moisture_pct\":%d,\"tilt_deg\":%.1f,\"vibration_level\":%d,\"battery_pct\":82,\"risk\":\"%s\",\"risk_score_demo\":%d,\"transport\":\"WOKWI_SOFTWARE_SIMULATION\"}\n",
      sequenceNumber, moisture, tiltDeg, vibration, a.level, a.score
    );
  }
  delay(30);
}
