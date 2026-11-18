#pragma once

/*
 * HimRakshak V2 - TinyML feature contract
 *
 * Destination:
 *   wokwi_esp32_demo/tinyml_feature_contract.h
 *
 * Purpose:
 *   - Names and documents the exact 16-input Decision Tree contract.
 *   - Delegates SensorFrame-to-array mapping to sensor_manager.h so there is
 *     only one implementation of the feature ordering.
 *   - Blocks inference when required measurements are invalid, stale, or
 *     non-finite.
 *   - Wraps the generated model prediction and its three class labels.
 *
 * IMPORTANT:
 *   Do not change feature order, units, or preprocessing without updating the
 *   training dataset/manifest, retraining, and regenerating HimRakshakModel.h.
 *   This header does not normalize or rescale features.
 */

#include <Arduino.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sensor_manager.h"
#include "HimRakshakModel.h"

namespace HimRakshakTinyMLContract {

// -----------------------------------------------------------------------------
// Canonical input order. These indices match makeTinyMlFeatures() in
// sensor_manager.h and the feature indices embedded in HimRakshakModel.h.
// -----------------------------------------------------------------------------
enum FeatureIndex : uint8_t {
  kRainfallIntensity = 0,
  kRainfall1Hour = 1,
  kRainfall24Hours = 2,
  kSurfaceMoisture = 3,
  kSubsurfaceMoisture = 4,
  kPorePressure = 5,
  kTiltX = 6,
  kTiltY = 7,
  kTiltRate = 8,
  kGroundDisplacement = 9,
  kDisplacementRate = 10,
  kVibrationRms = 11,
  kVibrationPeak = 12,
  kVibrationDominantFrequency = 13,
  kSoilTemperature = 14,
  kGroundwaterDepth = 15,
  kFeatureCount = 16
};

struct FeatureDescriptor {
  const char *name;
  const char *unit;
};

// Static metadata for logging, diagnostics, and future dashboard contracts.
// Keep this table in exactly the same order as FeatureIndex.
static const FeatureDescriptor kFeatureDescriptors[kFeatureCount] = {
    {"rainfall_intensity", "mm/h"},
    {"rainfall_1h", "mm"},
    {"rainfall_24h", "mm"},
    {"surface_moisture", "%"},
    {"subsurface_moisture", "%"},
    {"pore_pressure", "kPa"},
    {"tilt_x", "deg"},
    {"tilt_y", "deg"},
    {"tilt_rate", "deg/h"},
    {"ground_displacement", "mm"},
    {"displacement_rate", "mm/h"},
    {"vibration_rms", "g"},
    {"vibration_peak", "g"},
    {"vibration_dominant_frequency", "Hz"},
    {"soil_temperature", "degC"},
    {"groundwater_depth", "m"}
};

static const uint8_t kClassCount = 3U;

enum RiskClass : int8_t {
  kNormal = 0,
  kWarning = 1,
  kCritical = 2,
  kInvalidPrediction = -1
};

struct FeatureVector {
  float values[kFeatureCount];
  bool ready;
  const char *error;

  FeatureVector() : values{0.0f}, ready(false), error("not-built") {}
};

// -----------------------------------------------------------------------------
// Generated-model structural checks.
//
// This verifies that every non-leaf node refers to one of the 16 available
// input positions and that child indices remain inside the generated arrays.
// It is a structural consistency check, not a cryptographic hash verification.
// -----------------------------------------------------------------------------
inline bool modelStructureValid() {
  if (HimRakshakTinyML::NODE_COUNT <= 0) {
    return false;
  }

  for (int node = 0; node < HimRakshakTinyML::NODE_COUNT; ++node) {
    const int feature = HimRakshakTinyML::FEATURE[node];

    if (feature >= 0) {
      if (feature >= static_cast<int>(kFeatureCount)) {
        return false;
      }

      const int left = HimRakshakTinyML::LEFT_CHILD[node];
      const int right = HimRakshakTinyML::RIGHT_CHILD[node];
      if (left < 0 || left >= HimRakshakTinyML::NODE_COUNT ||
          right < 0 || right >= HimRakshakTinyML::NODE_COUNT) {
        return false;
      }
    } else {
      const int predicted = HimRakshakTinyML::PREDICTED_CLASS[node];
      if (predicted < kNormal || predicted > kCritical) {
        return false;
      }
    }
  }

  return true;
}

// -----------------------------------------------------------------------------
// Build the model input from the canonical SensorFrame.
//
// `isTinyMlFrameReady()` verifies the source/derived validity flags, including
// the 1-hour and 24-hour rainfall history. `makeTinyMlFeatures()` performs the
// sole field-to-index mapping. This function then rejects NaN/Inf values.
// -----------------------------------------------------------------------------
inline bool buildFeatureVector(
    const HimRakshakSensors::SensorFrame &frame,
    FeatureVector &out) {
  out.ready = false;
  out.error = "sensor-frame-not-ready";

  for (size_t i = 0; i < kFeatureCount; ++i) {
    out.values[i] = 0.0f;
  }

  if (!modelStructureValid()) {
    out.error = "model-structure-invalid";
    return false;
  }

  if (!HimRakshakSensors::isTinyMlFrameReady(frame)) {
    return false;
  }

  HimRakshakSensors::makeTinyMlFeatures(frame, out.values);

  for (size_t i = 0; i < kFeatureCount; ++i) {
    if (!isfinite(out.values[i])) {
      out.error = "non-finite-feature";
      return false;
    }
  }

  out.ready = true;
  out.error = nullptr;
  return true;
}

// Return the generated Decision Tree prediction, or -1 when the feature vector
// is not ready or the generated model returns an invalid class.
inline int predict(const FeatureVector &vector) {
  if (!vector.ready || !modelStructureValid()) {
    return kInvalidPrediction;
  }

  const int prediction = HimRakshakTinyML::predict(vector.values);
  if (prediction < kNormal || prediction > kCritical) {
    return kInvalidPrediction;
  }
  return prediction;
}

inline const char *className(int prediction) {
  switch (prediction) {
    case kNormal: return "NORMAL";
    case kWarning: return "WARNING";
    case kCritical: return "CRITICAL";
    default: return "NOT_READY_OR_INVALID";
  }
}

inline const char *featureName(size_t index) {
  return index < kFeatureCount ? kFeatureDescriptors[index].name : "unknown";
}

inline const char *featureUnit(size_t index) {
  return index < kFeatureCount ? kFeatureDescriptors[index].unit : "";
}

// Print the actual values which will be passed to the model. Call this only
// when needed for diagnostics; printing every loop iteration can disrupt the
// regular accelerometer sampling cadence.
inline void printFeatureVector(Stream &out, const FeatureVector &vector) {
  if (!vector.ready) {
    out.print(F("TinyML input NOT READY: "));
    out.println(vector.error != nullptr ? vector.error : "unknown-error");
    return;
  }

  out.println(F("[TINYML FEATURE VECTOR]"));
  for (size_t i = 0; i < kFeatureCount; ++i) {
    out.print(i);
    out.print(F(" | "));
    out.print(kFeatureDescriptors[i].name);
    out.print(F(" = "));
    out.print(vector.values[i], 5);
    out.print(F(" "));
    out.println(kFeatureDescriptors[i].unit);
  }
}

inline void printModelInfo(Stream &out) {
  out.println(F("[TINYML MODEL]"));
  out.print(F("Model ID: "));
  out.println(HimRakshakTinyML::MODEL_ID);
  out.print(F("Model SHA-256 (embedded metadata): "));
  out.println(HimRakshakTinyML::MODEL_SHA256);
  out.print(F("Input feature count: "));
  out.println(static_cast<unsigned int>(kFeatureCount));
  out.print(F("Tree nodes: "));
  out.println(HimRakshakTinyML::NODE_COUNT);
  out.print(F("Model structure: "));
  out.println(modelStructureValid() ? F("VALID") : F("INVALID"));
  out.println(F("Training data: synthetic demonstration data; not field-validated"));
}

}  // namespace HimRakshakTinyMLContract
