#pragma once

/*
 * HimRakshak V2 - sensor acquisition and feature engineering
 *
 * Destination:
 *   wokwi_esp32_demo/sensor_manager.h
 *
 * Required Arduino libraries (declared later in libraries.txt):
 *   - Adafruit MPU6050
 *   - Adafruit Unified Sensor
 *   - Adafruit BMP280 Library
 *   - DHT sensor library
 *   - OneWire
 *   - DallasTemperature
 *
 * This header is designed for the upgraded sketch. It intentionally does not
 * perform network operations or invoke the TinyML model. The caller obtains a
 * SensorFrame, checks isTinyMlFrameReady(), creates the fixed 16-feature vector,
 * and calls HimRakshakTinyML::predict().
 *
 * Important limitations:
 *   - Potentiometers are virtual proxies for moisture, pore pressure and
 *     displacement. The default conversion constants are demonstration
 *     calibration, not field calibration.
 *   - Live mode accumulates rainfall history only after boot and remains
 *     NOT READY until a full 24-hour observation window exists. Scenario mode
 *     explicitly assumes a synthetic zero-rain prior 24-hour baseline so the
 *     demonstration can exercise inference immediately; it is not field data.
 *   - MPU6050 vibration processing requires a continuous, sufficiently regular
 *     high-rate sample window (100 Hz live / 50 Hz scenario). Long blocking
 *     work in the acquisition task can invalidate it.
 *   - HC-SR04 depth is distance plus the configured installation offset. This
 *     is not automatically a groundwater depth unless the simulated sensor's
 *     reference geometry represents a groundwater observation point.
 *   - DHT22 and BMP280 readings are contextual only. They are not added to the
 *     existing 16-feature TinyML vector.
 */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "sensor_config.h"
#include "simulation_scenarios.h"

#ifndef HIMRAKSHAK_SIMULATE_SENSOR_FAULT
#define HIMRAKSHAK_SIMULATE_SENSOR_FAULT 0
#endif

#ifndef HIMRAKSHAK_DEMO_BATTERY_PCT
#define HIMRAKSHAK_DEMO_BATTERY_PCT 88
#endif

namespace HimRakshakSensors {

// -----------------------------------------------------------------------------
// Shared sensor frame. The legacy field names are retained for the existing
// packet/state code while the upgraded sketch is integrated.
// -----------------------------------------------------------------------------
struct SensorFrame {
  // Legacy/backend compatibility fields.
  int moisture = 0;
  float tiltDeg = 0.0f;
  int vibration = 0;
  int battery = HIMRAKSHAK_DEMO_BATTERY_PCT;
  bool healthy = false;

  // The existing model's 16 environmental input features.
  float rainfallIntensityMmH = 0.0f;
  float rainfall1hMm = 0.0f;
  float rainfall24hMm = 0.0f;
  float subsurfaceMoisturePct = 0.0f;
  float porePressureKpa = 0.0f;
  float tiltXDeg = 0.0f;
  float tiltYDeg = 0.0f;
  float tiltRateDegPerHour = 0.0f;
  float groundDisplacementMm = 0.0f;
  float groundDisplacementRateMmPerHour = 0.0f;
  float vibrationRmsG = 0.0f;
  float vibrationPeakG = 0.0f;
  float vibrationDominantHz = 0.0f;
  float soilTemperatureC = 0.0f;
  float groundwaterDepthM = 0.0f;

  // Additional environmental context; deliberately NOT model features.
  float airTemperatureC = 0.0f;
  float relativeHumidityPct = 0.0f;
  float atmosphericPressureHpa = 0.0f;

  // Source and derived-feature validity.
  bool rainfallHistoryValid = false;
  bool rainfall1hValid = false;
  bool rainfall24hValid = false;
  bool surfaceMoistureValid = false;
  bool subsurfaceMoistureValid = false;
  bool porePressureValid = false;
  bool mpu6050Valid = false;
  bool tiltRateValid = false;
  bool displacementValid = false;
  bool displacementRateValid = false;
  bool vibrationValid = false;
  bool soilTemperatureValid = false;
  bool groundwaterValid = false;
  bool dht22Valid = false;
  bool bmp280Valid = false;

  // Diagnostics/provenance helpers.
  bool adcRailSaturationObserved = false;
  uint32_t sampledAtMs = 0;
  uint16_t rainfallHistoryCoverageMinutes = 0;
  // Acquisition-task diagnostic values are copied into the frame so the
  // foreground loop never reads SensorManager's mutable internals concurrently.
  uint16_t diagnosticVibrationSamples = 0;
  uint8_t diagnosticTiltRateSamples = 0;
  uint8_t diagnosticDisplacementRateSamples = 0;
  bool diagnosticVibrationTimingValid = false;
  float diagnosticVibrationSampleRateHz = 0.0f;
  uint32_t diagnosticVibrationWindowDurationMs = 0U;
  uint8_t diagnosticVibrationInvalidReason = 1U;
  uint32_t diagnosticVibrationResetCount = 0U;
  uint32_t diagnosticLastGoodMpuAtMs = 0;
  uint32_t diagnosticLastGoodSoilAtMs = 0;
  uint32_t diagnosticLastGoodGroundwaterAtMs = 0;
  // True only in scenario mode: preceding 24 hours are explicitly assumed
  // to have zero rainfall so a repeatable demo can infer immediately.
  bool rainfallHistoryAssumedDryBaseline = false;
};

// -----------------------------------------------------------------------------
// ISR state. The ISR only counts debounced falling edges; all calculations
// happen later in normal task context.
// -----------------------------------------------------------------------------
static volatile uint32_t gRainPulseCount = 0;
static volatile uint32_t gRainLastPulseUs = 0;

void IRAM_ATTR rainfallPulseISR() {
  const uint32_t nowUs = micros();
  if (static_cast<uint32_t>(nowUs - gRainLastPulseUs) >=
      HimRakshakConfig::RainGauge::kDebounceIntervalMs * 1000UL) {
    gRainLastPulseUs = nowUs;
    ++gRainPulseCount;
  }
}

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------
inline float clampFloat(float value, float lower, float upper) {
  if (value < lower) return lower;
  if (value > upper) return upper;
  return value;
}

inline float mapLinear(int raw, int rawAtMinimum, int rawAtMaximum,
                       float outputMinimum, float outputMaximum) {
  if (rawAtMaximum == rawAtMinimum) return outputMinimum;
  const float fraction = static_cast<float>(raw - rawAtMinimum) /
                         static_cast<float>(rawAtMaximum - rawAtMinimum);
  return outputMinimum + fraction * (outputMaximum - outputMinimum);
}

inline bool isFiniteInRange(float value, float lower, float upper) {
  return isfinite(value) && value >= lower && value <= upper;
}

// -----------------------------------------------------------------------------
// Sensor manager
// -----------------------------------------------------------------------------
class SensorManager {
 public:
  SensorManager()
      : oneWire_(HimRakshakConfig::Pins::kDs18b20Data),
        ds18b20_(&oneWire_),
        dht_(HimRakshakConfig::Pins::kDht22Data, DHT22) {}

  // Starts the drivers. Returns true when the manager itself is initialized;
  // individual sensor availability is reported in the frame and diagnostics.
  bool begin() {
    using namespace HimRakshakConfig;

    analogReadResolution(Analog::kResolutionBits);
    pinMode(Pins::kSurfaceMoistureAdc, INPUT);
    pinMode(Pins::kSubsurfaceMoistureAdc, INPUT);
    pinMode(Pins::kPorePressureAdc, INPUT);
    pinMode(Pins::kGroundDisplacementAdc, INPUT);

    pinMode(Pins::kRainGaugePulse, INPUT_PULLUP);
    pinMode(Pins::kHcsr04Trigger, OUTPUT);
    pinMode(Pins::kHcsr04Echo, INPUT);
    digitalWrite(Pins::kHcsr04Trigger, LOW);

    Wire.begin(Pins::kI2cSda, Pins::kI2cScl);
    Wire.setClock(I2c::kClockHz);

    mpuReady_ = mpu_.begin(I2c::kMpu6050Address, &Wire);
    if (mpuReady_) {
      mpu_.setAccelerometerRange(MPU6050_RANGE_8_G);
      mpu_.setGyroRange(MPU6050_RANGE_500_DEG);
      mpu_.setFilterBandwidth(MPU6050_BAND_44_HZ);
    }

    // Wire.begin(SDA, SCL) has already initialized the default I2C bus.
    // The Wokwi-installed Adafruit_BMP280 API accepts (address, chip-id),
    // not (address, TwoWire*); use the default Wire bus here.
    bmpReady_ = bmp_.begin(I2c::kBmp280Address);
    dht_.begin();

    ds18b20_.begin();
    ds18b20_.setWaitForConversion(false);
    ds18b20_.requestTemperatures();
    dsConversionPending_ = true;
    dsRequestAtMs_ = millis();
    lastDsRequestAtMs_ = dsRequestAtMs_;

    attachInterrupt(digitalPinToInterrupt(Pins::kRainGaugePulse),
                    rainfallPulseISR, FALLING);

    lastUpdateAtMs_ = millis();
    lastAnalogReadAtMs_ = 0;
    lastMpuSampleAtMs_ = 0;
    lastDhtReadAtMs_ = 0;
    lastBmpReadAtMs_ = 0;
    lastHcsrReadAtMs_ = 0;
    lastMpuSampleAtMs_ = 0;
    vibrationSamplesCollected_ = 0U;
    vibrationWriteIndex_ = 0U;
    vibrationSamplesSinceMetrics_ = 0U;
    vibrationMetricsComputed_ = false;
    vibrationValid_ = false;
    vibrationTimingValid_ = true;
    vibrationMeasuredSampleRateHz_ = 0.0f;
    vibrationWindowDurationMs_ = 0U;
    vibrationInvalidReason_ = 1U;
    vibrationTimingResetCount_ = 0U;
    tiltInitializationSamples_ = 0U;
    tiltInitialSumX_ = tiltInitialSumY_ = tiltInitialSumZ_ = 0.0;
    tiltGravityInitialized_ = false;
    tiltEstimateValid_ = false;
    tiltRateValid_ = false;
    tiltRateSamples_ = 0U;
    previousTiltRateAtMs_ = 0U;
    displacementRateSamples_ = 0U;
    displacementRateValid_ = false;
    virtualElapsedMs_ = 0.0;
    virtualMinuteRemainderMs_ = 0.0;
    minuteBucketIndex_ = 0;
    historyCoverageMinutes_ = simulationEnabled_
        ? static_cast<uint16_t>(kRainMinuteBucketCount) : 0U;
    currentMinuteRainMm_ = 0.0f;
    for (size_t i = 0; i < kRainMinuteBucketCount; ++i) {
      // In scenario mode this is an explicit synthetic assumption: the
      // preceding 24 simulated hours are a zero-rain baseline. Live mode
      // remains unready until it has actually observed a complete window.
      rainMinuteBuckets_[i] = 0.0f;
    }

    // Reset the shared interrupt counter in a short critical section.
    noInterrupts();
    gRainPulseCount = 0;
    gRainLastPulseUs = 0;
    interrupts();
    lastHandledRainPulseCount_ = 0;
    lastHandledSimRainTipCount_ = simulationEnabled_
        ? simulationStimulus_.cumulativeRainTips : 0U;

    initialized_ = true;
    update();
    return initialized_;
  }

  // Call as frequently as possible from loop(); do not add a long delay.
  // Vibration sampling uses a mode-specific cadence: 10 ms live, 20 ms scenario.
  void update() {
    if (!initialized_) return;

    const uint32_t now = millis();
    const uint32_t wallDeltaMs = static_cast<uint32_t>(now - lastUpdateAtMs_);
    lastUpdateAtMs_ = now;
    advanceVirtualClock(wallDeltaMs);
    captureRainfallPulses(now);
    readAnalogInputs(now);
    sampleMpu6050(now);
    updateDs18b20(now);
    updateDht22(now);
    updateBmp280(now);
    updateHcsr04(now);
    updateDerivedRates(now);
    composeFrame(now);
  }

  // Select the virtual scenario source. Pass the current stimulus before each
  // update() call. When stimulus.enabled is false, physical Wokwi component
  // readings are used. The same conversion, filtering, derived-feature,
  // validity and TinyML path is used in both modes.
  void setSimulationStimulus(
      const HimRakshakSimulation::SensorStimulus &stimulus) {
    simulationStimulus_ = stimulus;
    simulationEnabled_ = stimulus.enabled;
  }

  const SensorFrame &frame() const { return frame_; }

  bool isTinyMlFrameReady() const {
    return frame_.healthy && frame_.rainfallHistoryValid &&
           frame_.surfaceMoistureValid && frame_.subsurfaceMoistureValid &&
           frame_.porePressureValid && frame_.mpu6050Valid &&
           frame_.tiltRateValid && frame_.displacementValid &&
           frame_.displacementRateValid && frame_.vibrationValid &&
           frame_.soilTemperatureValid && frame_.groundwaterValid;
  }

  bool mpu6050Detected() const { return mpuReady_; }
  bool bmp280Detected() const { return bmpReady_; }
  bool initialized() const { return initialized_; }

  // Print each readiness flag separately. This diagnostic is deliberately
  // read-only: it does not change the feature vector or relax model gating.
  void printReadinessDiagnostics(const SensorFrame &f,
                                 Stream &out = Serial) const {
    out.printf(
        "[SOURCE VALIDITY] healthy=%s rain_history=%s surface=%s subsurface=%s pore=%s mpu=%s tilt_rate=%s displacement=%s displacement_rate=%s vibration=%s soil_temp=%s groundwater=%s\n",
        f.healthy ? "YES" : "NO",
        f.rainfallHistoryValid ? "OK" : "FAIL",
        f.surfaceMoistureValid ? "OK" : "FAIL",
        f.subsurfaceMoistureValid ? "OK" : "FAIL",
        f.porePressureValid ? "OK" : "FAIL",
        f.mpu6050Valid ? "OK" : "FAIL",
        f.tiltRateValid ? "OK" : "FAIL",
        f.displacementValid ? "OK" : "FAIL",
        f.displacementRateValid ? "OK" : "FAIL",
        f.vibrationValid ? "OK" : "FAIL",
        f.soilTemperatureValid ? "OK" : "FAIL",
        f.groundwaterValid ? "OK" : "FAIL");
    out.printf(
        "[SOURCE VALIDITY] optional dht22=%s bmp280=%s adc_rail_saturation=%s assumed_dry_baseline=%s\n",
        f.dht22Valid ? "OK" : "FAIL",
        f.bmp280Valid ? "OK" : "FAIL",
        f.adcRailSaturationObserved ? "YES" : "NO",
        f.rainfallHistoryAssumedDryBaseline ? "YES" : "NO");
    char mpuAge[16];
    char soilAge[16];
    char groundwaterAge[16];
    const auto formatAge = [&f](char *buffer, size_t bufferSize,
                                uint32_t lastGoodAtMs) {
      if (lastGoodAtMs == 0U) {
        snprintf(buffer, bufferSize, "NEVER");
        return;
      }
      const uint32_t delta = static_cast<uint32_t>(f.sampledAtMs - lastGoodAtMs);
      // A small unsigned difference supports normal millis() wrap-around. A
      // huge difference means the snapshot timestamp appears older than the
      // last-good timestamp, so report a clock-order issue rather than a
      // misleading age near 2^32 milliseconds.
      if (delta > 0x7FFFFFFFUL) {
        snprintf(buffer, bufferSize, "CLOCK_ORDER");
      } else {
        snprintf(buffer, bufferSize, "%lu", static_cast<unsigned long>(delta));
      }
    };
    formatAge(mpuAge, sizeof(mpuAge), f.diagnosticLastGoodMpuAtMs);
    formatAge(soilAge, sizeof(soilAge), f.diagnosticLastGoodSoilAtMs);
    formatAge(groundwaterAge, sizeof(groundwaterAge),
              f.diagnosticLastGoodGroundwaterAtMs);
    const char *vibrationReason = "UNKNOWN";
    switch (f.diagnosticVibrationInvalidReason) {
      case 0U: vibrationReason = "VALID"; break;
      case 1U: vibrationReason = "WINDOW_WARMING"; break;
      case 2U: vibrationReason = "CADENCE_GAP_RESET"; break;
      case 3U: vibrationReason = "INVALID_MPU_SAMPLE"; break;
      case 4U: vibrationReason = "SAMPLE_RATE_OUT_OF_RANGE"; break;
      case 5U: vibrationReason = "NONFINITE_METRIC"; break;
      default: break;
    }
    out.printf(
        "[SOURCE DIAGNOSTICS] vibration_samples=%u/%u timing=%s sample_rate_hz=%.2f window_ms=%lu vibration_reason=%s resets=%lu tilt_rate_samples=%u displacement_rate_samples=%u mpu_age_ms=%s soil_age_ms=%s groundwater_age_ms=%s\n",
        static_cast<unsigned>(f.diagnosticVibrationSamples),
        static_cast<unsigned>(HimRakshakConfig::Timing::kVibrationSampleCount),
        f.diagnosticVibrationTimingValid ? "OK" : "RESET",
        f.diagnosticVibrationSampleRateHz,
        static_cast<unsigned long>(f.diagnosticVibrationWindowDurationMs),
        vibrationReason,
        static_cast<unsigned long>(f.diagnosticVibrationResetCount),
        static_cast<unsigned>(f.diagnosticTiltRateSamples),
        static_cast<unsigned>(f.diagnosticDisplacementRateSamples),
        mpuAge, soilAge, groundwaterAge);
  }

  // Print a concise hardware-detection report once during setup if desired.
  void printDiagnostics(Stream &out = Serial) const {
    out.println(F("[SENSOR INITIALIZATION]"));
    out.printf("Input source: %s\n", simulationEnabled_ ? "SCENARIO STIMULUS" : "LIVE VIRTUAL/HARDWARE COMPONENTS");
    out.printf("MPU6050: %s\n", (simulationEnabled_ || mpuReady_) ? "AVAILABLE" : "NOT DETECTED");
    out.printf("BMP280 : %s\n", (simulationEnabled_ || bmpReady_) ? "AVAILABLE" : "NOT DETECTED");
    out.println(F("DHT22  : driver initialized; waiting for first valid sample"));
    out.println(F("DS18B20: driver initialized; waiting for first conversion"));
    out.println(F("HC-SR04: GPIO trigger/echo configured; waiting for first echo"));
    out.println(F("Rain gauge: active-low pulse input attached"));
    out.println(F("Analog inputs: virtual calibrated proxies"));
    if (simulationEnabled_) {
      out.println(F("Rain history: synthetic zero-rain 24h baseline assumed for scenario demo"));
    } else {
      out.println(F("Rain history: measured from live startup; full 24h coverage required"));
    }
  }

 private:
  static constexpr size_t kRainMinuteBucketCount = 24U * 60U;
  static constexpr size_t kRainIntensityEventCapacity = 64U;
  static constexpr float kExpectedVibrationSampleHz =
      1000.0f / static_cast<float>(HimRakshakConfig::Timing::kVibrationSampleIntervalMs);
  static constexpr float kGravityMps2 = 9.80665f;
  static constexpr float kRadiansToDegrees = 57.2957795131f;
  static constexpr float kAnalogFilterAlpha = 0.25f;
  static constexpr float kRateFilterAlpha = 0.25f;
  static constexpr float kTiltRateFilterAlpha = 0.25f;
  static constexpr float kBaselineFilterAlpha = 0.015f;
  static constexpr float kTiltGravityFilterAlpha = 0.001f;

  struct RainIntensityEvent {
    uint32_t wallTimeMs = 0;
    bool occupied = false;
  };

  OneWire oneWire_;
  DallasTemperature ds18b20_;
  DHT dht_;
  Adafruit_MPU6050 mpu_;
  Adafruit_BMP280 bmp_;

  SensorFrame frame_;
  bool initialized_ = false;
  bool mpuReady_ = false;
  bool bmpReady_ = false;
  bool simulationEnabled_ = false;
  HimRakshakSimulation::SensorStimulus simulationStimulus_;

  uint32_t lastUpdateAtMs_ = 0;
  uint32_t lastAnalogReadAtMs_ = 0;
  uint32_t lastMpuSampleAtMs_ = 0;
  uint32_t lastDhtReadAtMs_ = 0;
  uint32_t lastBmpReadAtMs_ = 0;
  uint32_t lastHcsrReadAtMs_ = 0;
  enum class HcsrState : uint8_t { IDLE, WAIT_FOR_RISE, WAIT_FOR_FALL };
  HcsrState hcsrState_ = HcsrState::IDLE;
  uint32_t hcsrTriggerAtUs_ = 0;
  uint32_t hcsrEchoRiseAtUs_ = 0;

  // Analog readings and validity.
  float surfaceMoisturePct_ = 0.0f;
  float subsurfaceMoisturePct_ = 0.0f;
  float porePressureKpa_ = 0.0f;
  float displacementMm_ = 0.0f;
  bool surfaceMoistureValid_ = false;
  bool subsurfaceMoistureValid_ = false;
  bool porePressureValid_ = false;
  bool displacementValid_ = false;
  bool adcRailSaturationObserved_ = false;
  bool analogFilterInitialized_ = false;

  // MPU6050 tilt and vibration.
  float tiltXDeg_ = 0.0f;
  float tiltYDeg_ = 0.0f;
  float tiltRateDegPerHour_ = 0.0f;
  float previousTiltXForRateDeg_ = 0.0f;
  float previousTiltYForRateDeg_ = 0.0f;
  uint32_t previousTiltRateAtMs_ = 0;
  uint8_t tiltRateSamples_ = 0;
  bool tiltRateValid_ = false;
  bool mpuSampleValid_ = false;
  uint32_t lastGoodMpuAtMs_ = 0;

  float baselineAxG_ = 0.0f;
  float baselineAyG_ = 0.0f;
  float baselineAzG_ = 0.0f;
  bool accelerationBaselineInitialized_ = false;

  // Gravity estimate for tilt. It is initialized from a complete raw
  // acceleration window (so the first vibration phase is averaged out), then
  // tracked with a slow low-pass filter that rejects the vibration band.
  float tiltGravityAxG_ = 0.0f;
  float tiltGravityAyG_ = 0.0f;
  float tiltGravityAzG_ = 0.0f;
  double tiltInitialSumX_ = 0.0;
  double tiltInitialSumY_ = 0.0;
  double tiltInitialSumZ_ = 0.0;
  uint16_t tiltInitializationSamples_ = 0;
  bool tiltGravityInitialized_ = false;
  bool tiltEstimateValid_ = false;
  uint32_t lastTiltEstimateAtMs_ = 0;

  // Preserve each signed acceleration axis independently. Reducing the
  // three-axis signal to an unsigned magnitude before RMS/frequency analysis
  // rectifies the waveform, loses phase information and can underestimate
  // vibration energy while introducing frequency doubling.
  float vibrationSamplesX_[HimRakshakConfig::Timing::kVibrationSampleCount] = {};
  float vibrationSamplesY_[HimRakshakConfig::Timing::kVibrationSampleCount] = {};
  float vibrationSamplesZ_[HimRakshakConfig::Timing::kVibrationSampleCount] = {};
  uint32_t vibrationSampleTimesMs_[HimRakshakConfig::Timing::kVibrationSampleCount] = {};
  size_t vibrationWriteIndex_ = 0;
  size_t vibrationSamplesCollected_ = 0;
  uint8_t vibrationSamplesSinceMetrics_ = 0;
  bool vibrationMetricsComputed_ = false;
  float vibrationRmsG_ = 0.0f;
  float vibrationPeakG_ = 0.0f;
  float vibrationDominantHz_ = 0.0f;
  bool vibrationValid_ = false;
  bool vibrationTimingValid_ = true;
  float vibrationMeasuredSampleRateHz_ = 0.0f;
  uint32_t vibrationWindowDurationMs_ = 0U;
  uint8_t vibrationInvalidReason_ = 1U;
  uint32_t vibrationTimingResetCount_ = 0U;
  uint32_t vibrationLastSampleAtMs_ = 0;
  uint32_t vibrationWindowStartAtMs_ = 0;

  // Soil temperature asynchronous conversion.
  bool dsConversionPending_ = false;
  uint32_t dsRequestAtMs_ = 0;
  uint32_t lastDsRequestAtMs_ = 0;
  float soilTemperatureC_ = 0.0f;
  bool soilTemperatureValid_ = false;
  uint32_t lastGoodDsAtMs_ = 0;

  // DHT22 and BMP280 contextual data.
  float airTemperatureC_ = 0.0f;
  float relativeHumidityPct_ = 0.0f;
  float atmosphericPressureHpa_ = 0.0f;
  bool dhtValid_ = false;
  bool bmpValid_ = false;
  uint32_t lastGoodDhtAtMs_ = 0;
  uint32_t lastGoodBmpAtMs_ = 0;

  // Ultrasonic groundwater-distance measurement.
  float groundwaterDepthM_ = 0.0f;
  bool groundwaterValid_ = false;
  uint32_t lastGoodHcsrAtMs_ = 0;

  // Independently measured displacement rate.
  float displacementRateMmPerHour_ = 0.0f;
  float previousDisplacementMm_ = 0.0f;
  uint32_t previousDisplacementAtMs_ = 0;
  uint8_t displacementRateSamples_ = 0;
  bool displacementRateValid_ = false;

  // Rainfall intensity event window and 24-hour rolling history.
  uint32_t lastHandledRainPulseCount_ = 0;
  uint32_t lastHandledSimRainTipCount_ = 0;
  RainIntensityEvent rainEvents_[kRainIntensityEventCapacity];
  size_t rainEventWriteIndex_ = 0;
  float rainMinuteBuckets_[kRainMinuteBucketCount] = {};
  size_t minuteBucketIndex_ = 0;
  uint16_t historyCoverageMinutes_ = 0;
  double virtualElapsedMs_ = 0.0;
  double virtualMinuteRemainderMs_ = 0.0;
  float currentMinuteRainMm_ = 0.0f;
  float rainfallIntensityMmH_ = 0.0f;
  float rainfall1hMm_ = 0.0f;
  float rainfall24hMm_ = 0.0f;

  void advanceVirtualClock(uint32_t wallDeltaMs) {
    const double scale = static_cast<double>(HimRakshakConfig::Timing::kVirtualTimeScale);
    if (!(scale > 0.0)) return;

    const double simulatedDeltaMs = static_cast<double>(wallDeltaMs) * scale;
    virtualElapsedMs_ += simulatedDeltaMs;
    virtualMinuteRemainderMs_ += simulatedDeltaMs;

    while (virtualMinuteRemainderMs_ >= 60000.0) {
      virtualMinuteRemainderMs_ -= 60000.0;
      minuteBucketIndex_ = (minuteBucketIndex_ + 1U) % kRainMinuteBucketCount;
      rainMinuteBuckets_[minuteBucketIndex_] = 0.0f;
      currentMinuteRainMm_ = 0.0f;
      if (historyCoverageMinutes_ < kRainMinuteBucketCount) {
        ++historyCoverageMinutes_;
      }
    }

    rainfall1hMm_ = sumRecentRainMinutes(60U);
    rainfall24hMm_ = sumRecentRainMinutes(kRainMinuteBucketCount);
  }

  float sumRecentRainMinutes(size_t requestedMinutes) const {
    const size_t available = historyCoverageMinutes_ < kRainMinuteBucketCount
                                 ? static_cast<size_t>(historyCoverageMinutes_) + 1U
                                 : kRainMinuteBucketCount;
    const size_t count = requestedMinutes < available ? requestedMinutes : available;
    float total = 0.0f;
    for (size_t offset = 0; offset < count; ++offset) {
      const size_t index = (minuteBucketIndex_ + kRainMinuteBucketCount - offset) %
                           kRainMinuteBucketCount;
      total += rainMinuteBuckets_[index];
    }
    return total;
  }

  void captureRainfallPulses(uint32_t nowMs) {
    uint32_t newPulses = 0U;
    if (simulationEnabled_) {
      const uint32_t cumulative = simulationStimulus_.cumulativeRainTips;
      newPulses = static_cast<uint32_t>(
          cumulative - lastHandledSimRainTipCount_);
      lastHandledSimRainTipCount_ = cumulative;
    } else {
      uint32_t pulseSnapshot = 0;
      noInterrupts();
      pulseSnapshot = gRainPulseCount;
      interrupts();

      newPulses = static_cast<uint32_t>(
          pulseSnapshot - lastHandledRainPulseCount_);
      lastHandledRainPulseCount_ = pulseSnapshot;
    }

    for (uint32_t i = 0; i < newPulses; ++i) {
      const float tipMm = HimRakshakConfig::RainGauge::kMillimetersPerTip;
      rainMinuteBuckets_[minuteBucketIndex_] += tipMm;
      currentMinuteRainMm_ += tipMm;
      rainEvents_[rainEventWriteIndex_].wallTimeMs = nowMs;
      rainEvents_[rainEventWriteIndex_].occupied = true;
      rainEventWriteIndex_ = (rainEventWriteIndex_ + 1U) % kRainIntensityEventCapacity;
    }

    const uint32_t windowMs = HimRakshakConfig::RainGauge::kIntensityWindowMs;
    const double scale = static_cast<double>(HimRakshakConfig::Timing::kVirtualTimeScale);
    const double simulatedWindowHours =
        (static_cast<double>(windowMs) * scale) / 3600000.0;
    uint16_t tipsInWindow = 0;
    for (size_t i = 0; i < kRainIntensityEventCapacity; ++i) {
      if (rainEvents_[i].occupied &&
          static_cast<uint32_t>(nowMs - rainEvents_[i].wallTimeMs) <= windowMs) {
        ++tipsInWindow;
      }
    }
    rainfallIntensityMmH_ = simulatedWindowHours > 0.0
                                ? (tipsInWindow *
                                   HimRakshakConfig::RainGauge::kMillimetersPerTip) /
                                      static_cast<float>(simulatedWindowHours)
                                : 0.0f;
  }

  void readAnalogInputs(uint32_t nowMs) {
    if (lastAnalogReadAtMs_ != 0 &&
        static_cast<uint32_t>(nowMs - lastAnalogReadAtMs_) <
            HimRakshakConfig::Timing::kSensorFrameIntervalMs) {
      return;
    }
    lastAnalogReadAtMs_ = nowMs;

    using namespace HimRakshakConfig;
    const int surfaceRaw = simulationEnabled_
        ? simulationStimulus_.surfaceMoistureRaw
        : analogRead(Pins::kSurfaceMoistureAdc);
    const int subsurfaceRaw = simulationEnabled_
        ? simulationStimulus_.subsurfaceMoistureRaw
        : analogRead(Pins::kSubsurfaceMoistureAdc);
    const int pressureRaw = simulationEnabled_
        ? simulationStimulus_.porePressureRaw
        : analogRead(Pins::kPorePressureAdc);
    const int displacementRaw = simulationEnabled_
        ? simulationStimulus_.displacementRaw
        : analogRead(Pins::kGroundDisplacementAdc);

    const bool scenarioAnalogValid =
        !simulationEnabled_ || simulationStimulus_.analogInputsValid;
    const bool surfaceRawValid = scenarioAnalogValid &&
                                 surfaceRaw >= Analog::kRawMinimum &&
                                 surfaceRaw <= Analog::kRawMaximum;
    const bool subsurfaceRawValid = scenarioAnalogValid &&
                                    subsurfaceRaw >= Analog::kRawMinimum &&
                                    subsurfaceRaw <= Analog::kRawMaximum;
    const bool pressureRawValid = scenarioAnalogValid &&
                                  pressureRaw >= Analog::kRawMinimum &&
                                  pressureRaw <= Analog::kRawMaximum;
    const bool displacementRawValid = scenarioAnalogValid &&
                                      displacementRaw >= Analog::kRawMinimum &&
                                      displacementRaw <= Analog::kRawMaximum;

    adcRailSaturationObserved_ =
        surfaceRaw <= Analog::kFaultLowRailRaw ||
        surfaceRaw >= Analog::kFaultHighRailRaw ||
        subsurfaceRaw <= Analog::kFaultLowRailRaw ||
        subsurfaceRaw >= Analog::kFaultHighRailRaw ||
        pressureRaw <= Analog::kFaultLowRailRaw ||
        pressureRaw >= Analog::kFaultHighRailRaw ||
        displacementRaw <= Analog::kFaultLowRailRaw ||
        displacementRaw >= Analog::kFaultHighRailRaw;

    if (surfaceRawValid && subsurfaceRawValid && pressureRawValid &&
        displacementRawValid) {
      const float surface = clampFloat(
          mapLinear(surfaceRaw, Analog::kSurfaceMoistureRawAtZeroPct,
                    Analog::kSurfaceMoistureRawAtHundredPct, 0.0f, 100.0f),
          0.0f, 100.0f);
      const float subsurface = clampFloat(
          mapLinear(subsurfaceRaw, Analog::kSubsurfaceMoistureRawAtZeroPct,
                    Analog::kSubsurfaceMoistureRawAtHundredPct, 0.0f, 100.0f),
          0.0f, 100.0f);
      const float pressure = clampFloat(
          mapLinear(pressureRaw, Analog::kPorePressureRawAtZeroKpa,
                    Analog::kPorePressureRawAtFullScaleKpa, 0.0f,
                    Analog::kPorePressureFullScaleKpa),
          0.0f, Analog::kPorePressureFullScaleKpa);
      const float displacement = clampFloat(
          mapLinear(displacementRaw, Analog::kDisplacementRawAtZeroMm,
                    Analog::kDisplacementRawAtFullScaleMm, 0.0f,
                    Analog::kDisplacementFullScaleMm),
          0.0f, Analog::kDisplacementFullScaleMm);

      if (!analogFilterInitialized_) {
        surfaceMoisturePct_ = surface;
        subsurfaceMoisturePct_ = subsurface;
        porePressureKpa_ = pressure;
        displacementMm_ = displacement;
        analogFilterInitialized_ = true;
      } else {
        surfaceMoisturePct_ = kAnalogFilterAlpha * surface +
                              (1.0f - kAnalogFilterAlpha) * surfaceMoisturePct_;
        subsurfaceMoisturePct_ = kAnalogFilterAlpha * subsurface +
                                 (1.0f - kAnalogFilterAlpha) * subsurfaceMoisturePct_;
        porePressureKpa_ = kAnalogFilterAlpha * pressure +
                           (1.0f - kAnalogFilterAlpha) * porePressureKpa_;
        displacementMm_ = kAnalogFilterAlpha * displacement +
                          (1.0f - kAnalogFilterAlpha) * displacementMm_;
      }
      surfaceMoistureValid_ = true;
      subsurfaceMoistureValid_ = true;
      porePressureValid_ = true;
      displacementValid_ = true;
    } else {
      surfaceMoistureValid_ = surfaceRawValid;
      subsurfaceMoistureValid_ = subsurfaceRawValid;
      porePressureValid_ = pressureRawValid;
      displacementValid_ = displacementRawValid;
    }
  }

  void sampleMpu6050(uint32_t nowMs) {
    if (!simulationEnabled_ && !mpuReady_) {
      mpuSampleValid_ = false;
      vibrationValid_ = false;
      return;
    }
    if (simulationEnabled_ && !simulationStimulus_.mpu6050Valid) {
      mpuSampleValid_ = false;
      vibrationValid_ = false;
      vibrationSamplesCollected_ = 0U;
      vibrationWriteIndex_ = 0U;
      vibrationSamplesSinceMetrics_ = 0U;
      vibrationMetricsComputed_ = false;
      vibrationValid_ = false;
      vibrationTimingValid_ = false;
      vibrationInvalidReason_ = 3U;
      ++vibrationTimingResetCount_;
      tiltInitialSumX_ = tiltInitialSumY_ = tiltInitialSumZ_ = 0.0;
      tiltInitializationSamples_ = 0U;
      tiltGravityInitialized_ = false;
      tiltEstimateValid_ = false;
      lastTiltEstimateAtMs_ = 0U;
      previousTiltRateAtMs_ = 0U;
      tiltRateValid_ = false;
      tiltRateSamples_ = 0U;
      return;
    }

    // Wokwi's virtual ESP32 scheduler can introduce occasional >20 ms
    // task gaps even when the measured long-window cadence is otherwise
    // stable. For deterministic scenario stimuli, sample at 50 Hz (20 ms)
    // with a 2x cadence tolerance. The synthetic scenario waveforms are at or
    // below 12.5 Hz, so the 25 Hz Nyquist limit is sufficient for these tests.
    // Live MPU6050 mode retains the 100 Hz acquisition target and tighter
    // cadence gate for physical measurements.
    const uint32_t interval = simulationEnabled_
        ? 20UL
        : HimRakshakConfig::Timing::kVibrationSampleIntervalMs;
    if (lastMpuSampleAtMs_ != 0 &&
        static_cast<uint32_t>(nowMs - lastMpuSampleAtMs_) < interval) {
      return;
    }

    const uint32_t elapsedMs = lastMpuSampleAtMs_ == 0
        ? interval
        : static_cast<uint32_t>(nowMs - lastMpuSampleAtMs_);
    lastMpuSampleAtMs_ = nowMs;

    const float expectedMs = static_cast<float>(interval);
    // Do not stitch a materially missed sample into a frequency window.
    // Scenario mode's wider absolute interval follows its 50 Hz target;
    // live mode remains at 100 Hz.
    const bool cadenceOk = elapsedMs >= expectedMs * 0.50f &&
                           elapsedMs <= expectedMs * 2.00f;
    if (!cadenceOk) {
      // Reset only the vibration window. A single sampling gap must not erase
      // the independently filtered gravity/tilt estimator or its rate history.
      vibrationSamplesCollected_ = 0U;
      vibrationWriteIndex_ = 0U;
      vibrationSamplesSinceMetrics_ = 0U;
      vibrationMetricsComputed_ = false;
      vibrationValid_ = false;
      vibrationTimingValid_ = false;
      vibrationInvalidReason_ = 2U;
      ++vibrationTimingResetCount_;
    } else {
      vibrationTimingValid_ = true;
    }

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (simulationEnabled_) {
      ax = simulationStimulus_.accelerationXMps2 / kGravityMps2;
      ay = simulationStimulus_.accelerationYMps2 / kGravityMps2;
      az = simulationStimulus_.accelerationZMps2 / kGravityMps2;
    } else {
      sensors_event_t acceleration;
      sensors_event_t gyro;
      sensors_event_t temperature;
      mpu_.getEvent(&acceleration, &gyro, &temperature);
      ax = acceleration.acceleration.x / kGravityMps2;
      ay = acceleration.acceleration.y / kGravityMps2;
      az = acceleration.acceleration.z / kGravityMps2;
    }

    if (!isfinite(ax) || !isfinite(ay) || !isfinite(az)) {
      mpuSampleValid_ = false;
      vibrationValid_ = false;
      vibrationSamplesCollected_ = 0U;
      vibrationWriteIndex_ = 0U;
      vibrationSamplesSinceMetrics_ = 0U;
      vibrationMetricsComputed_ = false;
      vibrationInvalidReason_ = 3U;
      ++vibrationTimingResetCount_;
      return;
    }

    // Estimate the slowly varying gravity vector first. Use this filtered
    // vector for tilt so the 7-20 Hz vibration component does not appear as
    // enormous false tilt changes or saturate tilt-rate at its upper bound.
    if (!accelerationBaselineInitialized_) {
      baselineAxG_ = ax;
      baselineAyG_ = ay;
      baselineAzG_ = az;
      accelerationBaselineInitialized_ = true;
    } else {
      baselineAxG_ += kBaselineFilterAlpha * (ax - baselineAxG_);
      baselineAyG_ += kBaselineFilterAlpha * (ay - baselineAyG_);
      baselineAzG_ += kBaselineFilterAlpha * (az - baselineAzG_);
    }

    // Keep signed, per-axis AC acceleration. RMS and peak are calculated from
    // the three-axis vector, while frequency is estimated on the axis with the
    // strongest AC energy.
    const float dynamicX = ax - baselineAxG_;
    const float dynamicY = ay - baselineAyG_;
    const float dynamicZ = az - baselineAzG_;

    const size_t writeIndex = vibrationWriteIndex_;
    const size_t count = HimRakshakConfig::Timing::kVibrationSampleCount;

    // Tilt initialization has its own sample counter, independent of the
    // vibration ring buffer. A vibration cadence reset must not restart tilt.
    if (!tiltGravityInitialized_) {
      tiltInitialSumX_ += ax;
      tiltInitialSumY_ += ay;
      tiltInitialSumZ_ += az;
      if (tiltInitializationSamples_ < count) {
        ++tiltInitializationSamples_;
      }
      if (tiltInitializationSamples_ >= count) {
        tiltGravityAxG_ = static_cast<float>(tiltInitialSumX_ / count);
        tiltGravityAyG_ = static_cast<float>(tiltInitialSumY_ / count);
        tiltGravityAzG_ = static_cast<float>(tiltInitialSumZ_ / count);
        tiltGravityInitialized_ = true;
      }
    } else {
      tiltGravityAxG_ += kTiltGravityFilterAlpha * (ax - tiltGravityAxG_);
      tiltGravityAyG_ += kTiltGravityFilterAlpha * (ay - tiltGravityAyG_);
      tiltGravityAzG_ += kTiltGravityFilterAlpha * (az - tiltGravityAzG_);
    }

    vibrationSamplesX_[writeIndex] = dynamicX;
    vibrationSamplesY_[writeIndex] = dynamicY;
    vibrationSamplesZ_[writeIndex] = dynamicZ;
    vibrationSampleTimesMs_[writeIndex] = nowMs;
    vibrationWriteIndex_ = (vibrationWriteIndex_ + 1U) % count;
    if (vibrationSamplesCollected_ < count) {
      ++vibrationSamplesCollected_;
    }

    if (tiltGravityInitialized_) {
      const float gravityNorm = sqrtf(
          tiltGravityAxG_ * tiltGravityAxG_ +
          tiltGravityAyG_ * tiltGravityAyG_ +
          tiltGravityAzG_ * tiltGravityAzG_);
      if (isfinite(gravityNorm) && gravityNorm >= 0.20f) {
        tiltXDeg_ = atan2f(tiltGravityAyG_, tiltGravityAzG_) *
                    kRadiansToDegrees;
        tiltYDeg_ = atan2f(-tiltGravityAxG_,
            sqrtf(tiltGravityAyG_ * tiltGravityAyG_ +
                  tiltGravityAzG_ * tiltGravityAzG_)) * kRadiansToDegrees;
        tiltEstimateValid_ = true;
        lastTiltEstimateAtMs_ = nowMs;
      } else {
        tiltEstimateValid_ = false;
      }
    }

    mpuSampleValid_ = true;
    lastGoodMpuAtMs_ = nowMs;

    if (vibrationSamplesCollected_ == count && vibrationTimingValid_) {
      if (vibrationMetricsComputed_) {
        if (vibrationSamplesSinceMetrics_ < 255U) {
          ++vibrationSamplesSinceMetrics_;
        }
      }
      // Goertzel is considerably more expensive than acquisition. Compute it
      // once when the first complete window is ready, then refresh after every
      // 16 new samples instead of blocking the acquisition task per sample.
      if (!vibrationMetricsComputed_ || vibrationSamplesSinceMetrics_ >= 16U) {
        calculateVibrationMetrics();
        vibrationMetricsComputed_ = true;
        vibrationSamplesSinceMetrics_ = 0U;
      }
    } else {
      vibrationSamplesSinceMetrics_ = 0U;
      vibrationMetricsComputed_ = false;
      vibrationValid_ = false;
      // Preserve the reason that invalidated the current window (for example
      // CADENCE_GAP_RESET). On first boot the reason remains WINDOW_WARMING;
      // after a complete accepted window calculateVibrationMetrics() sets
      // VALID or a measurement-specific failure reason.
    }
  }

  void calculateVibrationMetrics() {
    const size_t count = HimRakshakConfig::Timing::kVibrationSampleCount;
    float meanX = 0.0f;
    float meanY = 0.0f;
    float meanZ = 0.0f;
    for (size_t i = 0; i < count; ++i) {
      const size_t index = (vibrationWriteIndex_ + i) % count;
      meanX += vibrationSamplesX_[index];
      meanY += vibrationSamplesY_[index];
      meanZ += vibrationSamplesZ_[index];
    }
    meanX /= static_cast<float>(count);
    meanY /= static_cast<float>(count);
    meanZ /= static_cast<float>(count);

    double vectorSquareSum = 0.0;
    double energyX = 0.0;
    double energyY = 0.0;
    double energyZ = 0.0;
    float vectorPeak = 0.0f;
    for (size_t i = 0; i < count; ++i) {
      const size_t index = (vibrationWriteIndex_ + i) % count;
      const float x = vibrationSamplesX_[index] - meanX;
      const float y = vibrationSamplesY_[index] - meanY;
      const float z = vibrationSamplesZ_[index] - meanZ;
      const float magnitude = sqrtf(x * x + y * y + z * z);
      vectorSquareSum += static_cast<double>(x) * x +
                         static_cast<double>(y) * y +
                         static_cast<double>(z) * z;
      energyX += static_cast<double>(x) * x;
      energyY += static_cast<double>(y) * y;
      energyZ += static_cast<double>(z) * z;
      if (magnitude > vectorPeak) vectorPeak = magnitude;
    }

    // Three-axis vector RMS (not the RMS of a rectified magnitude signal).
    vibrationRmsG_ = sqrtf(static_cast<float>(vectorSquareSum / count));
    vibrationPeakG_ = vectorPeak;

    const size_t oldestIndex = vibrationWriteIndex_;
    const size_t newestIndex = (vibrationWriteIndex_ + count - 1U) % count;
    const uint32_t windowDurationMs = static_cast<uint32_t>(
        vibrationSampleTimesMs_[newestIndex] -
        vibrationSampleTimesMs_[oldestIndex]);
    const float measuredSampleRateHz = windowDurationMs > 0U
        ? (static_cast<float>(count - 1U) * 1000.0f) /
              static_cast<float>(windowDurationMs)
        : 0.0f;
    vibrationWindowDurationMs_ = windowDurationMs;
    vibrationMeasuredSampleRateHz_ = measuredSampleRateHz;

    const float minimumAcceptedSampleRateHz = simulationEnabled_ ? 40.0f : 50.0f;
    const float maximumAcceptedSampleRateHz = simulationEnabled_ ? 75.0f : 200.0f;
    if (!isfinite(measuredSampleRateHz) ||
        measuredSampleRateHz < minimumAcceptedSampleRateHz ||
        measuredSampleRateHz > maximumAcceptedSampleRateHz) {
      vibrationValid_ = false;
      vibrationInvalidReason_ = 4U;
      return;
    }

    const float *dominantAxisSamples = vibrationSamplesX_;
    float dominantAxisMean = meanX;
    if (energyY > energyX && energyY >= energyZ) {
      dominantAxisSamples = vibrationSamplesY_;
      dominantAxisMean = meanY;
    } else if (energyZ > energyX && energyZ > energyY) {
      dominantAxisSamples = vibrationSamplesZ_;
      dominantAxisMean = meanZ;
    }

    vibrationDominantHz_ = calculateDominantFrequency(
        dominantAxisSamples, dominantAxisMean, measuredSampleRateHz);
    vibrationValid_ = isfinite(vibrationRmsG_) &&
                      isfinite(vibrationPeakG_) &&
                      isfinite(vibrationDominantHz_) &&
                      vibrationRmsG_ >= 0.0f && vibrationPeakG_ >= 0.0f;
    vibrationInvalidReason_ = vibrationValid_ ? 0U : 5U;
  }

  float calculateDominantFrequency(const float *samples, float mean,
                                   float sampleRate) const {
    const size_t count = HimRakshakConfig::Timing::kVibrationSampleCount;
    float bestPower = 0.0f;
    float bestFrequency = 0.0f;

    // Goertzel analysis on the signed axis with the strongest AC energy.
    // This avoids the frequency doubling caused by analysing abs(vector).
    for (size_t bin = 1; bin < count / 2U; ++bin) {
      const float omega = 2.0f * PI * static_cast<float>(bin) /
                          static_cast<float>(count);
      const float coefficient = 2.0f * cosf(omega);
      float q1 = 0.0f;
      float q2 = 0.0f;
      for (size_t n = 0; n < count; ++n) {
        const size_t index = (vibrationWriteIndex_ + n) % count;
        const float sample = samples[index] - mean;
        const float q0 = sample + coefficient * q1 - q2;
        q2 = q1;
        q1 = q0;
      }
      float power = q1 * q1 + q2 * q2 - coefficient * q1 * q2;
      if (power < 0.0f) power = 0.0f;
      if (power > bestPower) {
        bestPower = power;
        bestFrequency = static_cast<float>(bin) * sampleRate /
                        static_cast<float>(count);
      }
    }
    return bestPower < 0.000001f ? 0.0f : bestFrequency;
  }

  void updateDerivedRates(uint32_t nowMs) {
    const float scale = HimRakshakConfig::Timing::kVirtualTimeScale;

    // Tilt rate is intentionally evaluated at a slower cadence than the
    // vibration sampler. Dividing tiny high-frequency accelerometer
    // jitter by an accelerated interval creates huge false degrees/hour
    // values and can force the safety/model state to CRITICAL. Tilt comes from
    // a vibration-window-initialized, low-pass gravity estimate; this rate uses
    // a 3 s wall-time comparison window and a small angular deadband.
    static constexpr uint32_t kTiltRateWindowMs = 3000UL;
    static constexpr float kTiltRateNoiseDeadbandDeg = 0.10f;
    if (tiltEstimateValid_ && lastTiltEstimateAtMs_ == nowMs) {
      if (previousTiltRateAtMs_ == 0U) {
        previousTiltXForRateDeg_ = tiltXDeg_;
        previousTiltYForRateDeg_ = tiltYDeg_;
        previousTiltRateAtMs_ = nowMs;
      } else {
        const uint32_t wallDelta =
            static_cast<uint32_t>(nowMs - previousTiltRateAtMs_);
        if (wallDelta >= kTiltRateWindowMs) {
          const float elapsedHours =
              (static_cast<float>(wallDelta) * scale) / 3600000.0f;
          const float deltaX = tiltXDeg_ - previousTiltXForRateDeg_;
          const float deltaY = tiltYDeg_ - previousTiltYForRateDeg_;
          const float angularDelta = sqrtf(deltaX * deltaX + deltaY * deltaY);
          const float effectiveDelta =
              angularDelta <= kTiltRateNoiseDeadbandDeg ? 0.0f : angularDelta;
          if (elapsedHours > 0.0f) {
            const float rawRate = effectiveDelta / elapsedHours;
            const float boundedRate = clampFloat(rawRate, 0.0f, 12.0f);
            tiltRateDegPerHour_ = kTiltRateFilterAlpha * boundedRate +
                                  (1.0f - kTiltRateFilterAlpha) *
                                      tiltRateDegPerHour_;
            if (tiltRateSamples_ < 255U) ++tiltRateSamples_;
            // One completed rate interval is based on two independent valid
            // tilt estimates, so it is sufficient for the first valid rate.
            tiltRateValid_ = tiltRateSamples_ >= 1U;
          }
          previousTiltXForRateDeg_ = tiltXDeg_;
          previousTiltYForRateDeg_ = tiltYDeg_;
          previousTiltRateAtMs_ = nowMs;
        }
      }
    }

    // Ground displacement is sampled at the analog frame cadence, not on
    // every fast loop iteration.
    if (displacementValid_ && lastAnalogReadAtMs_ == nowMs) {
      if (previousDisplacementAtMs_ != 0) {
        const uint32_t wallDelta = static_cast<uint32_t>(nowMs - previousDisplacementAtMs_);
        const float elapsedHours = (static_cast<float>(wallDelta) * scale) /
                                   3600000.0f;
        if (elapsedHours > 0.0f) {
          const float rawRate = fabsf(displacementMm_ - previousDisplacementMm_) /
                                elapsedHours;
          const float boundedRate = clampFloat(rawRate, 0.0f, 12.0f);
          displacementRateMmPerHour_ = kRateFilterAlpha * boundedRate +
                                       (1.0f - kRateFilterAlpha) *
                                           displacementRateMmPerHour_;
          if (displacementRateSamples_ < 255U) ++displacementRateSamples_;
          displacementRateValid_ = displacementRateSamples_ >=
                                   HimRakshakConfig::Validity::kMinimumSamplesForRate;
        }
      }
      previousDisplacementMm_ = displacementMm_;
      previousDisplacementAtMs_ = nowMs;
    }
  }

  void updateDs18b20(uint32_t nowMs) {
    if (simulationEnabled_) {
      const float temperatureC = simulationStimulus_.soilTemperatureC;
      if (simulationStimulus_.soilTemperatureValid &&
          isFiniteInRange(temperatureC,
                          HimRakshakConfig::Validity::kDs18b20MinimumC,
                          HimRakshakConfig::Validity::kDs18b20MaximumC)) {
        soilTemperatureC_ = temperatureC;
        soilTemperatureValid_ = true;
        lastGoodDsAtMs_ = nowMs;
      } else {
        soilTemperatureValid_ = false;
      }
      dsConversionPending_ = false;
      return;
    }

    if (dsConversionPending_ &&
        static_cast<uint32_t>(nowMs - dsRequestAtMs_) >= 750UL) {
      const float temperatureC = ds18b20_.getTempCByIndex(0);
      if (temperatureC != DEVICE_DISCONNECTED_C &&
          isFiniteInRange(temperatureC,
                          HimRakshakConfig::Validity::kDs18b20MinimumC,
                          HimRakshakConfig::Validity::kDs18b20MaximumC)) {
        soilTemperatureC_ = temperatureC;
        soilTemperatureValid_ = true;
        lastGoodDsAtMs_ = nowMs;
      } else {
        soilTemperatureValid_ = false;
      }
      dsConversionPending_ = false;
    }

    if (!dsConversionPending_ &&
        static_cast<uint32_t>(nowMs - lastDsRequestAtMs_) >=
            HimRakshakConfig::Timing::kDs18b20IntervalMs) {
      ds18b20_.requestTemperatures();
      dsRequestAtMs_ = nowMs;
      lastDsRequestAtMs_ = nowMs;
      dsConversionPending_ = true;
    }
  }

  void updateDht22(uint32_t nowMs) {
    if (simulationEnabled_) {
      const float temperature = simulationStimulus_.airTemperatureC;
      const float humidity = simulationStimulus_.relativeHumidityPct;
      if (simulationStimulus_.dht22Valid &&
          isFiniteInRange(temperature,
                          HimRakshakConfig::Validity::kDht22MinimumTemperatureC,
                          HimRakshakConfig::Validity::kDht22MaximumTemperatureC) &&
          isFiniteInRange(humidity,
                          HimRakshakConfig::Validity::kDht22MinimumHumidityPct,
                          HimRakshakConfig::Validity::kDht22MaximumHumidityPct)) {
        airTemperatureC_ = temperature;
        relativeHumidityPct_ = humidity;
        dhtValid_ = true;
        lastGoodDhtAtMs_ = nowMs;
      } else {
        dhtValid_ = false;
      }
      return;
    }

    if (lastDhtReadAtMs_ != 0 &&
        static_cast<uint32_t>(nowMs - lastDhtReadAtMs_) <
            HimRakshakConfig::Timing::kDht22IntervalMs) {
      return;
    }
    lastDhtReadAtMs_ = nowMs;
    const float temperature = dht_.readTemperature();
    const float humidity = dht_.readHumidity();
    if (isFiniteInRange(temperature,
                        HimRakshakConfig::Validity::kDht22MinimumTemperatureC,
                        HimRakshakConfig::Validity::kDht22MaximumTemperatureC) &&
        isFiniteInRange(humidity,
                        HimRakshakConfig::Validity::kDht22MinimumHumidityPct,
                        HimRakshakConfig::Validity::kDht22MaximumHumidityPct)) {
      airTemperatureC_ = temperature;
      relativeHumidityPct_ = humidity;
      dhtValid_ = true;
      lastGoodDhtAtMs_ = nowMs;
    } else {
      dhtValid_ = false;
    }
  }

  void updateBmp280(uint32_t nowMs) {
    if (simulationEnabled_) {
      const float pressureHpa = simulationStimulus_.atmosphericPressureHpa;
      if (simulationStimulus_.bmp280Valid &&
          isFiniteInRange(pressureHpa,
                          HimRakshakConfig::Validity::kBmp280MinimumPressureHpa,
                          HimRakshakConfig::Validity::kBmp280MaximumPressureHpa)) {
        atmosphericPressureHpa_ = pressureHpa;
        bmpValid_ = true;
        lastGoodBmpAtMs_ = nowMs;
      } else {
        bmpValid_ = false;
      }
      return;
    }

    if (!bmpReady_ ||
        (lastBmpReadAtMs_ != 0 &&
         static_cast<uint32_t>(nowMs - lastBmpReadAtMs_) <
             HimRakshakConfig::Timing::kBmp280IntervalMs)) {
      return;
    }
    lastBmpReadAtMs_ = nowMs;
    const float pressureHpa = bmp_.readPressure() / 100.0f;
    if (isFiniteInRange(pressureHpa,
                        HimRakshakConfig::Validity::kBmp280MinimumPressureHpa,
                        HimRakshakConfig::Validity::kBmp280MaximumPressureHpa)) {
      atmosphericPressureHpa_ = pressureHpa;
      bmpValid_ = true;
      lastGoodBmpAtMs_ = nowMs;
    } else {
      bmpValid_ = false;
    }
  }

  void updateHcsr04(uint32_t nowMs) {
    if (simulationEnabled_) {
      const float distanceCm = simulationStimulus_.groundwaterDistanceCm;
      if (simulationStimulus_.groundwaterValid &&
          isFiniteInRange(distanceCm,
                          HimRakshakConfig::Groundwater::kHcsr04MinimumDistanceCm,
                          HimRakshakConfig::Groundwater::kHcsr04MaximumDistanceCm)) {
        const float depthM = distanceCm / 100.0f +
            HimRakshakConfig::Groundwater::kSensorFaceOffsetBelowGroundM;
        if (isFiniteInRange(depthM,
                            HimRakshakConfig::Groundwater::kMinimumDepthM,
                            HimRakshakConfig::Groundwater::kMaximumDepthM)) {
          groundwaterDepthM_ = depthM;
          groundwaterValid_ = true;
          lastGoodHcsrAtMs_ = nowMs;
        } else {
          groundwaterValid_ = false;
        }
      } else {
        groundwaterValid_ = false;
      }
      return;
    }

    // Non-blocking HC-SR04 state machine. pulseIn() can block for up to the
    // echo timeout and would disrupt the MPU6050 sampling cadence.
    using namespace HimRakshakConfig;
    const uint32_t nowUs = micros();

    if (hcsrState_ == HcsrState::IDLE) {
      if (lastHcsrReadAtMs_ == 0 ||
          static_cast<uint32_t>(nowMs - lastHcsrReadAtMs_) >=
              Timing::kHcsr04IntervalMs) {
        lastHcsrReadAtMs_ = nowMs;
        digitalWrite(Pins::kHcsr04Trigger, LOW);
        delayMicroseconds(2);
        digitalWrite(Pins::kHcsr04Trigger, HIGH);
        delayMicroseconds(10);
        digitalWrite(Pins::kHcsr04Trigger, LOW);
        hcsrTriggerAtUs_ = micros();
        hcsrState_ = HcsrState::WAIT_FOR_RISE;
      }
      return;
    }

    if (static_cast<uint32_t>(nowUs - hcsrTriggerAtUs_) >
        Groundwater::kEchoTimeoutUs) {
      groundwaterValid_ = false;
      hcsrState_ = HcsrState::IDLE;
      return;
    }

    if (hcsrState_ == HcsrState::WAIT_FOR_RISE) {
      if (digitalRead(Pins::kHcsr04Echo) == HIGH) {
        hcsrEchoRiseAtUs_ = nowUs;
        hcsrState_ = HcsrState::WAIT_FOR_FALL;
      }
      return;
    }

    if (hcsrState_ == HcsrState::WAIT_FOR_FALL &&
        digitalRead(Pins::kHcsr04Echo) == LOW) {
      const uint32_t durationUs =
          static_cast<uint32_t>(nowUs - hcsrEchoRiseAtUs_);
      hcsrState_ = HcsrState::IDLE;

      // Speed-of-sound approximation at ordinary room temperature.
      const float distanceCm = static_cast<float>(durationUs) * 0.0343f / 2.0f;
      if (!isFiniteInRange(distanceCm,
                           Groundwater::kHcsr04MinimumDistanceCm,
                           Groundwater::kHcsr04MaximumDistanceCm)) {
        groundwaterValid_ = false;
        return;
      }

      // This is water depth only when the sensor is aimed at a known water
      // surface from the configured ground reference datum.
      const float depthM = distanceCm / 100.0f +
                           Groundwater::kSensorFaceOffsetBelowGroundM;
      if (!isFiniteInRange(depthM, Groundwater::kMinimumDepthM,
                           Groundwater::kMaximumDepthM)) {
        groundwaterValid_ = false;
        return;
      }

      groundwaterDepthM_ = depthM;
      groundwaterValid_ = true;
      lastGoodHcsrAtMs_ = nowMs;
    }
  }

  void composeFrame(uint32_t nowMs) {
    frame_.moisture = static_cast<int>(roundf(surfaceMoisturePct_));
    frame_.tiltXDeg = tiltXDeg_;
    frame_.tiltYDeg = tiltYDeg_;
    frame_.tiltDeg = sqrtf(tiltXDeg_ * tiltXDeg_ + tiltYDeg_ * tiltYDeg_);
    frame_.vibration = static_cast<int>(roundf(vibrationPeakG_ * 100.0f));
    frame_.battery = HIMRAKSHAK_DEMO_BATTERY_PCT;

    frame_.rainfallIntensityMmH = rainfallIntensityMmH_;
    frame_.rainfall1hMm = rainfall1hMm_;
    frame_.rainfall24hMm = rainfall24hMm_;
    frame_.subsurfaceMoisturePct = subsurfaceMoisturePct_;
    frame_.porePressureKpa = porePressureKpa_;
    frame_.tiltRateDegPerHour = tiltRateDegPerHour_;
    frame_.groundDisplacementMm = displacementMm_;
    frame_.groundDisplacementRateMmPerHour = displacementRateMmPerHour_;
    frame_.vibrationRmsG = vibrationRmsG_;
    frame_.vibrationPeakG = vibrationPeakG_;
    frame_.vibrationDominantHz = vibrationDominantHz_;
    frame_.soilTemperatureC = soilTemperatureC_;
    frame_.groundwaterDepthM = groundwaterDepthM_;

    frame_.airTemperatureC = airTemperatureC_;
    frame_.relativeHumidityPct = relativeHumidityPct_;
    frame_.atmosphericPressureHpa = atmosphericPressureHpa_;

    frame_.rainfall1hValid = historyCoverageMinutes_ >= 60U;
    frame_.rainfall24hValid = historyCoverageMinutes_ >= kRainMinuteBucketCount;
    frame_.rainfallHistoryValid = frame_.rainfall1hValid && frame_.rainfall24hValid;
    frame_.surfaceMoistureValid = surfaceMoistureValid_;
    frame_.subsurfaceMoistureValid = subsurfaceMoistureValid_;
    frame_.porePressureValid = porePressureValid_;
    const uint32_t activeMpuSampleIntervalMs = simulationEnabled_
        ? 20UL
        : HimRakshakConfig::Timing::kVibrationSampleIntervalMs;
    frame_.mpu6050Valid = mpuSampleValid_ &&
        static_cast<uint32_t>(nowMs - lastGoodMpuAtMs_) <=
            3U * activeMpuSampleIntervalMs;
    frame_.tiltRateValid = tiltRateValid_;
    frame_.displacementValid = displacementValid_;
    frame_.displacementRateValid = displacementRateValid_;
    frame_.vibrationValid = vibrationValid_;
    frame_.soilTemperatureValid = soilTemperatureValid_ &&
        static_cast<uint32_t>(nowMs - lastGoodDsAtMs_) <= 3000UL;
    frame_.groundwaterValid = groundwaterValid_ &&
        static_cast<uint32_t>(nowMs - lastGoodHcsrAtMs_) <=
            2U * HimRakshakConfig::Timing::kHcsr04IntervalMs;
    frame_.dht22Valid = dhtValid_ &&
        static_cast<uint32_t>(nowMs - lastGoodDhtAtMs_) <= 3U *
            HimRakshakConfig::Timing::kDht22IntervalMs;
    frame_.bmp280Valid = bmpValid_ &&
        static_cast<uint32_t>(nowMs - lastGoodBmpAtMs_) <= 3U *
            HimRakshakConfig::Timing::kBmp280IntervalMs;

    frame_.adcRailSaturationObserved = adcRailSaturationObserved_;
    frame_.sampledAtMs = nowMs;
    frame_.rainfallHistoryCoverageMinutes = historyCoverageMinutes_;
    frame_.rainfallHistoryAssumedDryBaseline = simulationEnabled_;
    frame_.diagnosticVibrationSamples = static_cast<uint16_t>(
        vibrationSamplesCollected_);
    frame_.diagnosticTiltRateSamples = tiltRateSamples_;
    frame_.diagnosticDisplacementRateSamples = displacementRateSamples_;
    frame_.diagnosticVibrationTimingValid = vibrationTimingValid_;
    frame_.diagnosticVibrationSampleRateHz = vibrationMeasuredSampleRateHz_;
    frame_.diagnosticVibrationWindowDurationMs = vibrationWindowDurationMs_;
    frame_.diagnosticVibrationInvalidReason = vibrationInvalidReason_;
    frame_.diagnosticVibrationResetCount = vibrationTimingResetCount_;
    frame_.diagnosticLastGoodMpuAtMs = lastGoodMpuAtMs_;
    frame_.diagnosticLastGoodSoilAtMs = lastGoodDsAtMs_;
    frame_.diagnosticLastGoodGroundwaterAtMs = lastGoodHcsrAtMs_;

    const bool coreSourcesValid =
        frame_.surfaceMoistureValid && frame_.subsurfaceMoistureValid &&
        frame_.porePressureValid && frame_.mpu6050Valid &&
        frame_.soilTemperatureValid && frame_.groundwaterValid &&
        frame_.displacementValid && frame_.displacementRateValid &&
        frame_.tiltRateValid && frame_.vibrationValid;
    frame_.healthy = coreSourcesValid && !HIMRAKSHAK_SIMULATE_SENSOR_FAULT;
  }
};

// Fixed feature mapping used by the existing Decision Tree. Do not reorder or
// change units unless the dataset, manifest and exported model are retrained.
inline void makeTinyMlFeatures(const SensorFrame &frame, float features[16]) {
  features[0] = frame.rainfallIntensityMmH;
  features[1] = frame.rainfall1hMm;
  features[2] = frame.rainfall24hMm;
  features[3] = static_cast<float>(frame.moisture);
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

inline bool isTinyMlFrameReady(const SensorFrame &frame) {
  return frame.healthy && frame.rainfallHistoryValid &&
         frame.surfaceMoistureValid && frame.subsurfaceMoistureValid &&
         frame.porePressureValid && frame.mpu6050Valid &&
         frame.tiltRateValid && frame.displacementValid &&
         frame.displacementRateValid && frame.vibrationValid &&
         frame.soilTemperatureValid && frame.groundwaterValid;
}

}  // namespace HimRakshakSensors
