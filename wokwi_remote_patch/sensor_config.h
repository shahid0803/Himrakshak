#pragma once

/*
 * HimRakshak V2 - virtual/physical sensor configuration
 *
 * Destination:
 *   wokwi_esp32_demo/sensor_config.h
 *
 * This file centralizes GPIO assignments, sensor addresses, acquisition
 * intervals, and the default calibration ranges used by sensor_manager.h.
 *
 * IMPORTANT:
 * - These defaults are for the Wokwi demonstration circuit.
 * - Potentiometers represent calibrated sensor proxies; they are not real
 *   moisture, pore-pressure, or displacement transducers.
 * - The analog full-scale values below retain the broad units used by the
 *   current firmware. Confirm them against the model training manifest before
 *   claiming physical or field calibration.
 * - Do not change the 16-feature order or rescale values without retraining
 *   and re-exporting the TinyML model.
 */

#include <Arduino.h>

namespace HimRakshakConfig {

// -----------------------------------------------------------------------------
// ESP32 GPIO assignments
// -----------------------------------------------------------------------------
namespace Pins {

// ADC1 inputs: these pins remain usable while the ESP32 Wi-Fi radio is active.
// GPIO34, GPIO35, GPIO36 and GPIO39 are input-only pins on the classic ESP32.
static constexpr uint8_t kSurfaceMoistureAdc = 34;
static constexpr uint8_t kGroundDisplacementAdc = 35;
static constexpr uint8_t kSubsurfaceMoistureAdc = 36;
static constexpr uint8_t kPorePressureAdc = 39;

// Digital sensor and event inputs.
static constexpr uint8_t kRainGaugePulse = 18;  // Active-low pulse to GND.
static constexpr uint8_t kDs18b20Data = 4;      // 1-Wire data; use 4.7k pull-up.
static constexpr uint8_t kHcsr04Trigger = 16;
static constexpr uint8_t kHcsr04Echo = 17;
static constexpr uint8_t kDht22Data = 19;

// Shared I2C bus: MPU6050 and BMP280 use separate I2C addresses.
static constexpr uint8_t kI2cSda = 21;
static constexpr uint8_t kI2cScl = 22;

// Local status indicators and audible alert.
static constexpr uint8_t kGreenLed = 25;
static constexpr uint8_t kYellowLed = 26;
static constexpr uint8_t kRedLed = 27;
static constexpr uint8_t kBuzzer = 23;

}  // namespace Pins

// -----------------------------------------------------------------------------
// I2C addresses
// -----------------------------------------------------------------------------
namespace I2c {
static constexpr uint8_t kMpu6050Address = 0x68;
static constexpr uint8_t kBmp280Address = 0x76;
static constexpr uint32_t kClockHz = 400000UL;
}  // namespace I2c

// -----------------------------------------------------------------------------
// Acquisition and scheduling
// -----------------------------------------------------------------------------
namespace Timing {

static constexpr uint32_t kSerialBaud = 115200UL;

// Main sensor-frame update. Accelerometer sampling uses its own faster cadence.
static constexpr uint32_t kSensorFrameIntervalMs = 100UL;
static constexpr uint32_t kDiagnosticIntervalMs = 1000UL;
static constexpr uint32_t kTelemetryIntervalMs = 10000UL;

// Live MPU6050 vibration sampling: 100 samples/second, 128 samples/window.
// Scenario mode uses a 20 ms (50 Hz) virtual sampling cadence in SensorManager
// to avoid Wokwi scheduler jitter. Its synthetic waveforms are <=12.5 Hz.
// A live 100 Hz window is about 1.28 s; a scenario 50 Hz window about 2.56 s.
static constexpr uint32_t kVibrationSampleIntervalMs = 10UL;
static constexpr size_t kVibrationSampleCount = 128U;
static constexpr uint32_t kVibrationWindowMs =
    kVibrationSampleIntervalMs * kVibrationSampleCount;

// Sensor-specific update intervals. DHT22 must not be polled faster than its
// supported measurement cadence. DS18B20 conversion handling is asynchronous
// in the sensor driver; the driver must not block the main firmware loop.
static constexpr uint32_t kDht22IntervalMs = 2000UL;
static constexpr uint32_t kDs18b20IntervalMs = 1000UL;
static constexpr uint32_t kBmp280IntervalMs = 1000UL;
static constexpr uint32_t kHcsr04IntervalMs = 250UL;

// Rainfall-history demo clock: one wall-clock second represents one simulated
// minute. Set this to 1.0 for real-time hardware operation. All rate and rolling
// window calculations must use this same time base consistently.
static constexpr float kVirtualTimeScale = 60.0f;

}  // namespace Timing

// -----------------------------------------------------------------------------
// ADC configuration and analog-proxy calibration
// -----------------------------------------------------------------------------
namespace Analog {

static constexpr uint8_t kResolutionBits = 12;
static constexpr int kRawMinimum = 0;
static constexpr int kRawMaximum = 4095;

// Values very close to either rail are treated as suspicious by the sensor
// driver. This is only a rail-saturation heuristic: an ADC alone cannot prove
// that a sensor wire is disconnected. Move the Wokwi potentiometers away from
// their hard endpoints during normal demonstrations.
static constexpr int kFaultLowRailRaw = 4;
static constexpr int kFaultHighRailRaw = 4091;

// Wokwi potentiometer calibration defaults. Increasing ADC raw value increases
// the displayed measurement. For real sensors, replace these endpoints with
// calibration readings obtained from the actual sensor and reference standard.
static constexpr int kSurfaceMoistureRawAtZeroPct = 0;
static constexpr int kSurfaceMoistureRawAtHundredPct = 4095;
static constexpr int kSubsurfaceMoistureRawAtZeroPct = 0;
static constexpr int kSubsurfaceMoistureRawAtHundredPct = 4095;

// Demonstration mapping retained from the current project firmware:
// 0..4095 ADC counts -> 0..160 kPa. This is not a physical transducer
// calibration; it is a virtual pressure-proxy scale.
static constexpr int kPorePressureRawAtZeroKpa = 0;
static constexpr int kPorePressureRawAtFullScaleKpa = 4095;
static constexpr float kPorePressureFullScaleKpa = 160.0f;

// Independent virtual displacement input. The 25 mm range is a configurable
// demonstration span, not a claim about a real slope/displacement instrument.
static constexpr int kDisplacementRawAtZeroMm = 0;
static constexpr int kDisplacementRawAtFullScaleMm = 4095;
static constexpr float kDisplacementFullScaleMm = 25.0f;

}  // namespace Analog

// -----------------------------------------------------------------------------
// Rain gauge configuration
// -----------------------------------------------------------------------------
namespace RainGauge {

// Each active-low button pulse represents one simulated tipping-bucket event.
// 0.2 mm/tip is a configurable demonstration assumption. A real rain gauge
// must use the manufacturer's calibrated tip volume/area value.
static constexpr float kMillimetersPerTip = 0.2f;
static constexpr uint32_t kDebounceIntervalMs = 35UL;
static constexpr uint32_t kIntensityWindowMs = 10000UL;

}  // namespace RainGauge

// -----------------------------------------------------------------------------
// Groundwater distance configuration
// -----------------------------------------------------------------------------
namespace Groundwater {

static constexpr float kHcsr04MinimumDistanceCm = 2.0f;
static constexpr float kHcsr04MaximumDistanceCm = 400.0f;
static constexpr uint32_t kEchoTimeoutUs = 30000UL;

// Geometry convention:
// The HC-SR04 is assumed to face down from a reference point at the ground
// surface. Therefore groundwater depth below ground = measured air gap.
// If the sensor face is installed below/above another reference datum, update
// this signed offset after defining the physical installation geometry.
static constexpr float kSensorFaceOffsetBelowGroundM = 0.0f;

// Existing project telemetry/model range clamp. This is a software range,
// not an HC-SR04 calibration or a guarantee that the well is physically 10 m.
static constexpr float kMinimumDepthM = 0.3f;
static constexpr float kMaximumDepthM = 10.0f;

}  // namespace Groundwater

// -----------------------------------------------------------------------------
// Temperature and sensor validity limits
// -----------------------------------------------------------------------------
namespace Validity {

static constexpr float kDs18b20MinimumC = -55.0f;
static constexpr float kDs18b20MaximumC = 125.0f;

static constexpr float kDht22MinimumTemperatureC = -40.0f;
static constexpr float kDht22MaximumTemperatureC = 80.0f;
static constexpr float kDht22MinimumHumidityPct = 0.0f;
static constexpr float kDht22MaximumHumidityPct = 100.0f;

static constexpr float kBmp280MinimumPressureHpa = 300.0f;
static constexpr float kBmp280MaximumPressureHpa = 1100.0f;

// A derived feature/rate must not be treated as valid until at least two
// timestamped samples have been collected.
static constexpr uint8_t kMinimumSamplesForRate = 2;

}  // namespace Validity

}  // namespace HimRakshakConfig
