#pragma once

/*
 * HimRakshak V2 - deterministic Wokwi scenario signal generator
 *
 * Destination:
 *   wokwi_esp32_demo/simulation_scenarios.h
 *
 * Purpose:
 *   Generate reproducible sensor-like INPUT SIGNALS for six Wokwi scenarios.
 *   The generated signals are intended to be fed through SensorManager's
 *   ordinary conversion, filtering, derived-feature, validity and TinyML path.
 *   This file does NOT call the model and does NOT force an output class.
 *
 * Integration contract for the upcoming sensor_manager.h revision:
 *   1. Construct HimRakshakSimulation::ScenarioManager.
 *   2. Call begin(selectedScenario, millis()) in setup().
 *   3. Call update(millis()) on every loop iteration.
 *   4. Pass stimulus() into SensorManager as an optional simulation source.
 *   5. SensorManager must use the raw ADC / accelerometer / temperature /
 *      distance inputs below before running its existing processing routines.
 *
 * All generated measurements are synthetic simulation signals, not physical
 * observations. Do not present them as calibrated field sensor measurements.
 *
 * Important model limitation verified against the current generated
 * HimRakshakModel.h: the present Decision Tree does not split on feature 15
 * (groundwater_depth). The GROUNDWATER_CHANGE scenario therefore demonstrates
 * groundwater measurement acquisition; it cannot be expected to change the
 * model class by groundwater depth alone.
 */

#include <math.h>
#include <stdint.h>

#include "sensor_config.h"

namespace HimRakshakSimulation {

enum class Scenario : uint8_t {
  kLiveHardware = 0,
  kNormal,
  kHeavyRain,
  kSlopeMovement,
  kVibrationBurst,
  kGroundwaterChange,
  kSensorFailure,
  kRemoteCustom
};

// Values delivered by the landing page via the backend command queue. They are
// virtual stimulus values, not physical sensor measurements.
struct RemoteSensorValues {
  bool active = false;
  float rainfallIntensityMmH = 8.0f;
  float surfaceMoisturePct = 32.0f;
  float subsurfaceMoisturePct = 36.0f;
  float porePressureKpa = 28.0f;
  float tiltXDeg = 1.2f;
  float tiltYDeg = 0.2f;
  float groundDisplacementMm = 2.0f;
  float vibrationRmsG = 0.02f;
  float vibrationFrequencyHz = 8.0f;
  float soilTemperatureC = 17.0f;
  float groundwaterDepthM = 6.8f;
  float airTemperatureC = 19.0f;
  float relativeHumidityPct = 56.0f;
  float atmosphericPressureHpa = 1012.0f;
};

struct SensorStimulus {
  bool enabled = false;
  Scenario scenario = Scenario::kLiveHardware;

  // Raw ADC values for the existing analog conversion path.
  int surfaceMoistureRaw = 2048;
  int subsurfaceMoistureRaw = 2048;
  int porePressureRaw = 1024;
  int displacementRaw = 256;

  // Monotonic cumulative simulated rain-gauge tip count. SensorManager should
  // convert counter deltas into rainfall events using the same configured
  // mm/tip constant and rolling-window implementation as hardware pulses.
  uint32_t cumulativeRainTips = 0;

  // MPU6050 accelerometer values in m/s^2. These include gravity/orientation
  // plus a deterministic vibration waveform. The regular MPU processing path
  // should consume these values instead of mpu_.getEvent() in simulation mode.
  float accelerationXMps2 = 0.0f;
  float accelerationYMps2 = 0.0f;
  float accelerationZMps2 = 9.80665f;

  // Other sensor outputs. These still pass through the driver's normal range
  // and validity checks when SensorManager consumes the stimulus.
  float soilTemperatureC = 16.0f;
  float groundwaterDistanceCm = 250.0f;
  float airTemperatureC = 18.0f;
  float relativeHumidityPct = 55.0f;
  float atmosphericPressureHpa = 1013.25f;

  // Simulated device/source availability. These flags are NOT model outputs.
  bool analogInputsValid = true;
  bool mpu6050Valid = true;
  bool soilTemperatureValid = true;
  bool groundwaterValid = true;
  bool dht22Valid = true;
  bool bmp280Valid = true;
};

class ScenarioManager {
 public:
  ScenarioManager() = default;

  void begin(Scenario selected, uint32_t nowMs) {
    scenario_ = selected;
    startedAtMs_ = nowMs;
    lastUpdateAtMs_ = nowMs;
    initialized_ = true;
    cumulativeRainTips_ = 0U;
    fractionalRainTips_ = 0.0;
    lastStimulus_ = SensorStimulus();
    update(nowMs);
  }

  // Switching scenarios preserves the cumulative rain-tip counter. This avoids
  // a counter rollback when SensorManager consumes cumulative counter deltas.
  void setScenario(Scenario selected, uint32_t nowMs) {
    if (!initialized_) {
      begin(selected, nowMs);
      return;
    }
    scenario_ = selected;
    startedAtMs_ = nowMs;
    lastUpdateAtMs_ = nowMs;
    fractionalRainTips_ = 0.0;
    update(nowMs);
  }

  void setRemoteValues(const RemoteSensorValues &values) {
    remoteValues_ = values;
    remoteValues_.active = true;
  }

  void clearRemoteValues() {
    remoteValues_ = RemoteSensorValues();
    remoteValues_.active = false;
  }

  void update(uint32_t nowMs) {
    if (!initialized_) {
      begin(Scenario::kLiveHardware, nowMs);
      return;
    }

    const uint32_t deltaWallMs =
        static_cast<uint32_t>(nowMs - lastUpdateAtMs_);
    lastUpdateAtMs_ = nowMs;

    if (scenario_ == Scenario::kLiveHardware) {
      lastStimulus_ = SensorStimulus();
      lastStimulus_.enabled = false;
      lastStimulus_.scenario = Scenario::kLiveHardware;
      return;
    }

    const double timeScale =
        static_cast<double>(HimRakshakConfig::Timing::kVirtualTimeScale);
    const double elapsedSimulatedMs =
        static_cast<double>(static_cast<uint32_t>(nowMs - startedAtMs_)) *
        timeScale;
    const double elapsedHours = elapsedSimulatedMs / 3600000.0;
    const double elapsedSeconds = elapsedSimulatedMs / 1000.0;
    const double elapsedWallSeconds =
        static_cast<double>(static_cast<uint32_t>(nowMs - startedAtMs_)) / 1000.0;

    updateRainCounter(deltaWallMs, rainfallRateForScenario(), timeScale);
    composeStimulus(nowMs, elapsedHours, elapsedSeconds, elapsedWallSeconds);
  }

  const SensorStimulus &stimulus() const { return lastStimulus_; }
  Scenario scenario() const { return scenario_; }
  bool enabled() const { return scenario_ != Scenario::kLiveHardware; }
  bool initialized() const { return initialized_; }

  const char *scenarioName() const { return nameOf(scenario_); }
  const char *scenarioDescription() const {
    switch (scenario_) {
      case Scenario::kLiveHardware:
        return "Live Wokwi component inputs; no software stimulus override";
      case Scenario::kNormal:
        return "Stable simulated slope with low rainfall and low vibration";
      case Scenario::kHeavyRain:
        return "Increasing simulated rainfall and moisture with moderate motion";
      case Scenario::kSlopeMovement:
        return "Independent displacement ramp and changing two-axis tilt";
      case Scenario::kVibrationBurst:
        return "Deterministic sampled acceleration waveform for vibration analysis";
      case Scenario::kGroundwaterChange:
        return "Changing ultrasonic distance with otherwise stable slope inputs";
      case Scenario::kSensorFailure:
        return "Selected sensor sources unavailable; inference must be inhibited";
      case Scenario::kRemoteCustom:
        return "Landing-page values applied to virtual sensor inputs";
      default:
        return "Unknown simulation scenario";
    }
  }

  static Scenario fromName(const char *name) {
    if (name == nullptr) return Scenario::kLiveHardware;
    if (equalsIgnoreCase(name, "normal")) return Scenario::kNormal;
    if (equalsIgnoreCase(name, "heavy_rain") ||
        equalsIgnoreCase(name, "heavy-rain")) return Scenario::kHeavyRain;
    if (equalsIgnoreCase(name, "slope_movement") ||
        equalsIgnoreCase(name, "slope_shift") ||
        equalsIgnoreCase(name, "slope-movement")) {
      return Scenario::kSlopeMovement;
    }
    if (equalsIgnoreCase(name, "vibration_burst") ||
        equalsIgnoreCase(name, "vibration-burst")) {
      return Scenario::kVibrationBurst;
    }
    if (equalsIgnoreCase(name, "groundwater_change") ||
        equalsIgnoreCase(name, "groundwater-change")) {
      return Scenario::kGroundwaterChange;
    }
    if (equalsIgnoreCase(name, "custom") ||
        equalsIgnoreCase(name, "remote_custom")) return Scenario::kRemoteCustom;
    if (equalsIgnoreCase(name, "sensor_failure") ||
        equalsIgnoreCase(name, "sensor-failure") ||
        equalsIgnoreCase(name, "sensor_fault")) {
      return Scenario::kSensorFailure;
    }
    if (equalsIgnoreCase(name, "live") ||
        equalsIgnoreCase(name, "hardware")) {
      return Scenario::kLiveHardware;
    }
    return Scenario::kLiveHardware;
  }

  static const char *nameOf(Scenario scenario) {
    switch (scenario) {
      case Scenario::kLiveHardware: return "live_hardware";
      case Scenario::kNormal: return "normal";
      case Scenario::kHeavyRain: return "heavy_rain";
      case Scenario::kSlopeMovement: return "slope_movement";
      case Scenario::kVibrationBurst: return "vibration_burst";
      case Scenario::kGroundwaterChange: return "groundwater_change";
      case Scenario::kSensorFailure: return "sensor_failure";
      case Scenario::kRemoteCustom: return "custom";
      default: return "unknown";
    }
  }

 private:
  static constexpr float kGravityMps2 = 9.80665f;
  static constexpr float kDegreesToRadians = 0.017453292519943295f;
  static constexpr float kTwoPi = 6.2831853071795864769f;

  Scenario scenario_ = Scenario::kLiveHardware;
  bool initialized_ = false;
  uint32_t startedAtMs_ = 0;
  uint32_t lastUpdateAtMs_ = 0;
  uint32_t cumulativeRainTips_ = 0;
  double fractionalRainTips_ = 0.0;
  SensorStimulus lastStimulus_;
  RemoteSensorValues remoteValues_;

  static bool equalsIgnoreCase(const char *left, const char *right) {
    if (left == nullptr || right == nullptr) return false;
    while (*left != '\0' && *right != '\0') {
      char a = *left++;
      char b = *right++;
      if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
      if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
  }

  static float clamp(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
  }

  static float smoothStep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    float t = clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  static int adcFromPercent(float percent) {
    const float bounded = clamp(percent, 0.0f, 100.0f);
    return static_cast<int>(lroundf(bounded * 4095.0f / 100.0f));
  }

  static int adcFromPressureKpa(float pressureKpa) {
    const float fullScale =
        HimRakshakConfig::Analog::kPorePressureFullScaleKpa;
    const float bounded = clamp(pressureKpa, 0.0f, fullScale);
    return static_cast<int>(lroundf(
        bounded * 4095.0f / fullScale));
  }

  static int adcFromDisplacementMm(float displacementMm) {
    const float fullScale =
        HimRakshakConfig::Analog::kDisplacementFullScaleMm;
    const float bounded = clamp(displacementMm, 0.0f, fullScale);
    return static_cast<int>(lroundf(
        bounded * 4095.0f / fullScale));
  }

  float rainfallRateForScenario() const {
    if (remoteValues_.active) return clamp(remoteValues_.rainfallIntensityMmH, 0.0f, 200.0f);
    switch (scenario_) {
      case Scenario::kHeavyRain: return 60.0f;       // synthetic mm/hour
      case Scenario::kSlopeMovement: return 18.0f; // synthetic mm/hour
      case Scenario::kVibrationBurst: return 2.0f;
      case Scenario::kGroundwaterChange: return 0.0f;
      case Scenario::kSensorFailure: return 8.0f;
      case Scenario::kNormal:
      case Scenario::kLiveHardware:
      default: return 0.0f;
    }
  }

  void updateRainCounter(uint32_t deltaWallMs, float rainfallMmPerHour,
                         double timeScale) {
    if (deltaWallMs == 0U || rainfallMmPerHour <= 0.0f ||
        !(timeScale > 0.0)) {
      return;
    }

    const double simulatedHours =
        (static_cast<double>(deltaWallMs) * timeScale) / 3600000.0;
    const double rainMm = static_cast<double>(rainfallMmPerHour) *
                          simulatedHours;
    const double tipSize = static_cast<double>(
        HimRakshakConfig::RainGauge::kMillimetersPerTip);
    if (!(tipSize > 0.0)) return;

    fractionalRainTips_ += rainMm / tipSize;
    const uint32_t wholeTips =
        static_cast<uint32_t>(fractionalRainTips_);
    if (wholeTips > 0U) {
      cumulativeRainTips_ += wholeTips;
      fractionalRainTips_ -= static_cast<double>(wholeTips);
    }
  }

  void composeStimulus(uint32_t nowMs, double elapsedHours,
                       double elapsedSeconds, double elapsedWallSeconds) {
    SensorStimulus next;
    next.enabled = true;
    next.scenario = scenario_;
    next.cumulativeRainTips = cumulativeRainTips_;

    // Deterministic small analog drift makes consecutive frames realistic while
    // remaining reproducible for regression tests.
    const float seconds = static_cast<float>(elapsedSeconds);
    const float wallSeconds = static_cast<float>(elapsedWallSeconds);
    const float slowWave = sinf(kTwoPi * seconds / 47.0f);
    const float fastWave = sinf(kTwoPi * seconds / 13.0f);

    float surfaceMoisture = 34.0f + 0.8f * slowWave;
    float subsurfaceMoisture = 39.0f + 0.5f * fastWave;
    float porePressure = 28.0f + 0.8f * slowWave;
    float displacement = 2.0f + 0.15f * static_cast<float>(elapsedHours);
    float tiltX = 0.35f;
    float tiltY = -0.20f;
    float vibrationAmplitudeG = 0.018f;
    float vibrationFrequencyHz = 8.0f;
    float soilTemperature = 17.0f + 0.2f * slowWave;
    float groundwaterDepthM = 2.50f;
    float airTemperature = 19.0f + 0.4f * slowWave;
    float humidity = 56.0f + 1.0f * fastWave;
    float pressureHpa = 1012.0f + 0.4f * slowWave;

    switch (scenario_) {
      case Scenario::kNormal:
        // Deliberately keep the current Decision Tree's active risk features
        // below its synthetic demonstration split thresholds where practical.
        break;

      case Scenario::kHeavyRain: {
        const float ramp = smoothStep(0.0f, 0.20f, seconds);
        surfaceMoisture = 48.0f + 24.0f * ramp;
        subsurfaceMoisture = 50.0f + 22.0f * ramp;
        porePressure = 42.0f + 38.0f * ramp;
        displacement = 3.0f + 1.0f * ramp;
        // Heavy rain is an environmental loading scenario, not a slope-motion
        // scenario. Keep the gravity orientation stable so vibration does not
        // masquerade as tilt or create an artificial tilt-rate hazard.
        tiltX = 0.80f;
        tiltY = 0.10f;
        // This synthetic signal intentionally exercises the model's current
        // vibration-RMS branch. It is not a learned causal relationship between
        // rainfall and vibration.
        vibrationAmplitudeG = 0.36f;
        vibrationFrequencyHz = 7.8f;
        soilTemperature = 17.5f + 0.3f * ramp;
        groundwaterDepthM = 2.3f - 0.1f * ramp;
        humidity = 78.0f + 8.0f * ramp;
        pressureHpa = 1008.0f - 1.0f * ramp;
        break;
      }

      case Scenario::kSlopeMovement: {
        // Displacement is an independent signal. At the configured virtual
        // clock scale, this ramp is approximately 3 mm per simulated hour.
        displacement = 2.0f + 3.0f * static_cast<float>(elapsedHours);
        displacement = clamp(displacement, 2.0f, 23.0f);
        const float movementRamp = smoothStep(0.0f, 0.10f, seconds);
        tiltX = 0.45f + 1.8f * movementRamp +
                0.5f * static_cast<float>(elapsedHours);
        tiltY = -0.20f + 0.65f * movementRamp;
        surfaceMoisture = 55.0f + 3.0f * movementRamp;
        subsurfaceMoisture = 59.0f + 4.0f * movementRamp;
        porePressure = 58.0f + 16.0f * movementRamp;
        vibrationAmplitudeG = 0.08f;
        vibrationFrequencyHz = 9.5f;
        groundwaterDepthM = 2.0f - 0.15f * movementRamp;
        break;
      }

      case Scenario::kVibrationBurst: {
        // Burst envelope follows wall time, not the accelerated environmental
        // clock. Otherwise a 60x demo clock makes the burst finish before the
        // 100 Hz vibration sampler can collect a complete signal window.
        const float burst = smoothStep(0.5f, 1.5f, wallSeconds) *
                            (1.0f - smoothStep(8.0f, 10.0f, wallSeconds));
        vibrationAmplitudeG = 0.42f * burst + 0.018f;
        vibrationFrequencyHz = 12.5f;
        tiltX = 0.35f;
        tiltY = -0.20f;
        break;
      }

      case Scenario::kGroundwaterChange: {
        // Groundwater depth changes independently; the remaining slope inputs
        // stay close to normal to reveal whether the trained model actually
        // uses feature 15. The current generated tree does not split on it.
        const float drawdown = smoothStep(0.0f, 30.0f, seconds);
        groundwaterDepthM = 2.6f - 1.6f * drawdown;
        surfaceMoisture = 34.0f + 1.0f * slowWave;
        subsurfaceMoisture = 39.0f + 1.0f * fastWave;
        porePressure = 28.0f + 1.0f * slowWave;
        displacement = 2.0f + 0.15f * static_cast<float>(elapsedHours);
        vibrationAmplitudeG = 0.018f;
        break;
      }

      case Scenario::kRemoteCustom:
        // The remote values below replace these baseline defaults.
        break;

      case Scenario::kSensorFailure:
        // Keep finite placeholder values for diagnostics, but explicitly mark
        // required sources unavailable. The TinyML contract must inhibit
        // inference; the independent safety layer should raise SENSOR_FAULT.
        next.analogInputsValid = false;
        next.mpu6050Valid = false;
        next.soilTemperatureValid = false;
        next.groundwaterValid = false;
        break;

      case Scenario::kLiveHardware:
      default:
        next.enabled = false;
        next.scenario = Scenario::kLiveHardware;
        lastStimulus_ = next;
        return;
    }

    if (remoteValues_.active) {
      surfaceMoisture = clamp(remoteValues_.surfaceMoisturePct, 0.0f, 100.0f);
      subsurfaceMoisture = clamp(remoteValues_.subsurfaceMoisturePct, 0.0f, 100.0f);
      porePressure = clamp(remoteValues_.porePressureKpa, 0.0f,
          HimRakshakConfig::Analog::kPorePressureFullScaleKpa);
      displacement = clamp(remoteValues_.groundDisplacementMm, 0.0f,
          HimRakshakConfig::Analog::kDisplacementFullScaleMm);
      tiltX = clamp(remoteValues_.tiltXDeg, -15.0f, 15.0f);
      tiltY = clamp(remoteValues_.tiltYDeg, -15.0f, 15.0f);
      // Three-axis sinusoidal stimulus is scaled so the measured resultant RMS
      // remains close to the dashboard's requested RMS value.
      vibrationAmplitudeG = clamp(remoteValues_.vibrationRmsG, 0.0f, 1.5f) * 0.81649658f;
      vibrationFrequencyHz = clamp(remoteValues_.vibrationFrequencyHz, 0.0f, 20.0f);
      soilTemperature = clamp(remoteValues_.soilTemperatureC, -10.0f, 60.0f);
      groundwaterDepthM = clamp(remoteValues_.groundwaterDepthM, 0.2f, 20.0f);
      airTemperature = clamp(remoteValues_.airTemperatureC, -20.0f, 60.0f);
      humidity = clamp(remoteValues_.relativeHumidityPct, 0.0f, 100.0f);
      pressureHpa = clamp(remoteValues_.atmosphericPressureHpa, 800.0f, 1100.0f);
      next.analogInputsValid = true;
      next.mpu6050Valid = true;
      next.soilTemperatureValid = true;
      next.groundwaterValid = true;
    }

    // Convert engineering-unit proxy values to raw ADC counts. The manager
    // then applies the same configured calibration/conversion path as in live
    // hardware mode.
    next.surfaceMoistureRaw = adcFromPercent(surfaceMoisture);
    next.subsurfaceMoistureRaw = adcFromPercent(subsurfaceMoisture);
    next.porePressureRaw = adcFromPressureKpa(porePressure);
    next.displacementRaw = adcFromDisplacementMm(displacement);

    // Build a gravity vector whose two-axis inclination agrees with the
    // accelerometer equations used in SensorManager. Add a deterministic
    // three-axis sinusoidal vibration waveform to the gravity components.
    const float xRad = tiltX * kDegreesToRadians;
    const float yRad = tiltY * kDegreesToRadians;
    const float baseAx = -sinf(yRad) * kGravityMps2;
    const float baseAy = sinf(xRad) * cosf(yRad) * kGravityMps2;
    const float baseAz = cosf(xRad) * cosf(yRad) * kGravityMps2;
    // Vibration is a sampled waveform: its frequency must be expressed in
    // real wall-clock seconds so a 100 Hz sampler observes the intended Hz.
    const float phase = kTwoPi * vibrationFrequencyHz * wallSeconds;
    const float vibrationX = vibrationAmplitudeG * sinf(phase);
    const float vibrationY = vibrationAmplitudeG * sinf(phase * 1.13f + 0.7f);
    const float vibrationZ = vibrationAmplitudeG * sinf(phase * 0.91f + 1.4f);
    next.accelerationXMps2 = baseAx + vibrationX * kGravityMps2;
    next.accelerationYMps2 = baseAy + vibrationY * kGravityMps2;
    next.accelerationZMps2 = baseAz + vibrationZ * kGravityMps2;

    next.soilTemperatureC = soilTemperature;
    next.groundwaterDistanceCm = groundwaterDepthM * 100.0f -
        HimRakshakConfig::Groundwater::kSensorFaceOffsetBelowGroundM * 100.0f;
    next.airTemperatureC = airTemperature;
    next.relativeHumidityPct = clamp(humidity, 0.0f, 100.0f);
    next.atmosphericPressureHpa = pressureHpa;

    // The fault scenario deliberately invalidates only required sources.
    // Context sensors remain separately represented and may still be valid.
    if (scenario_ == Scenario::kSensorFailure) {
      next.dht22Valid = true;
      next.bmp280Valid = true;
    }

    lastStimulus_ = next;
    (void)nowMs;
  }
};

}  // namespace HimRakshakSimulation
