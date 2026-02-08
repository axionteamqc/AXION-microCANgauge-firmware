#include "user_sensors/user_sensors.h"

#include <cstring>

SignalId sourceToSignal(UserSensorSource src) {
  switch (src) {
    case UserSensorSource::kSensor2:
      return SignalId::kSensors2;
    case UserSensorSource::kSensor1:
    default:
      return SignalId::kSensors1;
  }
}

UserSensorKind presetToKind(UserSensorPreset preset) {
  switch (preset) {
    case UserSensorPreset::kOilTemp:
      return UserSensorKind::kTemp;
    case UserSensorPreset::kFuelPressure:
    case UserSensorPreset::kOilPressure:
      return UserSensorKind::kPressure;
    case UserSensorPreset::kCustomVoltage:
      return UserSensorKind::kVoltage;
    case UserSensorPreset::kCustomUnitless:
    default:
      return UserSensorKind::kUnitless;
  }
}

const char* defaultLabel(UserSensorPreset preset) {
  switch (preset) {
    case UserSensorPreset::kOilPressure:
      return "OILP";
    case UserSensorPreset::kOilTemp:
      return "OILT";
    case UserSensorPreset::kFuelPressure:
      return "FUELP";
    case UserSensorPreset::kCustomVoltage:
      return "VOLT";
    case UserSensorPreset::kCustomUnitless:
    default:
      return "SENS";
  }
}

void applyPresetDefaults(UserSensorCfg& cfg) {
  cfg.kind = presetToKind(cfg.preset);
  cfg.scale = 1.0f;
  cfg.offset = 0.0f;
  strlcpy(cfg.label, defaultLabel(cfg.preset), sizeof(cfg.label));
  cfg.unit_metric[0] = '\0';
  cfg.unit_imperial[0] = '\0';
}

bool computeCanonical(const UserSensorCfg& cfg, float decoded, float& canon_out) {
  canon_out = (decoded * cfg.scale) + cfg.offset;
  (void)cfg;
  return true;
}

void formatDisplay(const UserSensorCfg& cfg, bool imperial_units, float canon,
                   float& out_value, const char*& out_unit) {
  static const char* kUnitKpa = "kPa";
  static const char* kUnitPsi = "psi";
  static const char* kUnitC = "C";
  static const char* kUnitF = "F";
  static const char* kUnitV = "V";

  switch (cfg.kind) {
    case UserSensorKind::kPressure:
      if (imperial_units) {
        out_value = canon * 0.145038f;  // kPa -> psi
        out_unit = kUnitPsi;
      } else {
        out_value = canon;
        out_unit = kUnitKpa;
      }
      break;
    case UserSensorKind::kTemp:
      if (imperial_units) {
        out_value = canon;  // already Fahrenheit
        out_unit = kUnitF;
      } else {
        out_value = (canon - 32.0f) * (5.0f / 9.0f);
        out_unit = kUnitC;
      }
      break;
    case UserSensorKind::kVoltage:
      out_value = canon;
      out_unit = kUnitV;
      break;
    case UserSensorKind::kUnitless:
    default:
      out_value = canon;
      out_unit = imperial_units ? cfg.unit_imperial : cfg.unit_metric;
      if (!out_unit) {
        out_unit = "";
      } else if (out_unit[0] == '\0') {
        out_unit = "";
      }
      break;
  }
}

