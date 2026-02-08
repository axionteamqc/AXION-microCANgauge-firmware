#include "settings/nvs_store.h"

#include <Preferences.h>
#include <cmath>
#include <cstring>

#include "config/factory_config.h"
#include "config/logging.h"

bool NvsStore::PutUCharChecked(Preferences& prefs, const char* key, uint8_t value) {
  if (prefs.putUChar(key, value) == 0) return false;
#if NVS_VERIFY_WRITES
  const uint8_t got = prefs.getUChar(key, static_cast<uint8_t>(value ^ 0xFFu));
  if (got != value) {
    LOGE("NVS verify failed: %s\r\n", key);
    return false;
  }
#endif
  return true;
}

bool NvsStore::PutUShortChecked(Preferences& prefs, const char* key, uint16_t value) {
  if (prefs.putUShort(key, value) == 0) return false;
#if NVS_VERIFY_WRITES
  const uint16_t got =
      prefs.getUShort(key, static_cast<uint16_t>(value ^ 0xFFFFu));
  if (got != value) {
    LOGE("NVS verify failed: %s\r\n", key);
    return false;
  }
#endif
  return true;
}

bool NvsStore::PutUIntChecked(Preferences& prefs, const char* key, uint32_t value) {
  if (prefs.putUInt(key, value) == 0) return false;
#if NVS_VERIFY_WRITES
  const uint32_t got = prefs.getUInt(key, value ^ 0xFFFFFFFFu);
  if (got != value) {
    LOGE("NVS verify failed: %s\r\n", key);
    return false;
  }
#endif
  return true;
}

bool NvsStore::PutBoolChecked(Preferences& prefs, const char* key, bool value) {
  if (prefs.putBool(key, value) == 0) return false;
#if NVS_VERIFY_WRITES
  const bool got = prefs.getBool(key, !value);
  if (got != value) {
    LOGE("NVS verify failed: %s\r\n", key);
    return false;
  }
#endif
  return true;
}

bool NvsStore::PutFloatChecked(Preferences& prefs, const char* key, float value) {
  if (prefs.putFloat(key, value) == 0) return false;
#if NVS_VERIFY_WRITES
  const float got = prefs.getFloat(key, NAN);
  const bool ok = (isnan(value) && isnan(got)) || (got == value);
  if (!ok) {
    LOGE("NVS verify failed: %s\r\n", key);
    return false;
  }
#endif
  return true;
}

bool NvsStore::PutStringChecked(Preferences& prefs, const char* key, const char* value) {
  const char* expected = value ? value : "";
  const size_t expected_len = strlen(expected);
  if (prefs.putString(key, expected) != expected_len) return false;
#if NVS_VERIFY_WRITES
  String got = prefs.getString(key, "\x01");
  if (got.length() != expected_len || !got.equals(expected)) {
    LOGE("NVS verify failed: %s\r\n", key);
    return false;
  }
#endif
  return true;
}

NvsStore::NvsStore() : ready_(false) {}

bool NvsStore::begin() {
  Preferences prefs;
  ready_ = prefs.begin(kNamespace, false);
  return ready_;
}

bool NvsStore::loadCanSettings(CanSettings& out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  out.bitrate_locked = prefs.getBool(kKeyLocked, false);
  out.bitrate_value = prefs.getUInt(kKeyBitrate, out.bitrate_value);
  out.id_present_mask = static_cast<uint8_t>(prefs.getUChar(kKeyIdMask, 0));
  out.schema_version = static_cast<uint8_t>(prefs.getUChar(kKeyCanVer, out.schema_version));
  out.ecu_profile_id = static_cast<uint8_t>(prefs.getUChar(kKeyCanProfile, out.ecu_profile_id));
  const String stored_hash = prefs.getString(kKeyHash, "");
  out.hash_match = stored_hash.equals(kDbcSha256);
  if (!out.hash_match) {
    out.bitrate_locked = false;
  }
  prefs.end();
  return true;
}

bool NvsStore::saveCanSettings(const CanSettings& in) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  prefs.putUChar(kKeyCanVer, in.schema_version);
  prefs.putBool(kKeyLocked, in.bitrate_locked);
  prefs.putUInt(kKeyBitrate, in.bitrate_value);
  prefs.putUChar(kKeyIdMask, in.id_present_mask);
  prefs.putUChar(kKeyCanProfile, in.ecu_profile_id);
  prefs.putString(kKeyHash, kDbcSha256);
  prefs.end();
  // Verify write
  CanSettings verify = in;
  if (!loadCanSettings(verify)) return false;
  const bool ok = (verify.bitrate_locked == in.bitrate_locked) &&
                  (verify.bitrate_value == in.bitrate_value) &&
                  (verify.id_present_mask == in.id_present_mask) &&
                  (verify.ecu_profile_id == in.ecu_profile_id) &&
                  verify.hash_match;
  return ok;
}

bool NvsStore::loadSetupPersist(SetupPersist& out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  out.phase = static_cast<uint8_t>(prefs.getUChar(kKeyWizPhase, 0));
  out.baro_acquired = prefs.getBool(kKeyBaroOk, false);
  out.baro_kpa = prefs.getFloat(kKeyBaroKpa, 0.0f);
  out.stale_ms[0] = prefs.getUShort(kKeyStale0, 800);
  out.stale_ms[1] = prefs.getUShort(kKeyStale1, 800);
  out.stale_ms[2] = prefs.getUShort(kKeyStale2, 800);
  out.stale_ms[3] = prefs.getUShort(kKeyStale3, 800);
  out.stale_ms[4] = prefs.getUShort(kKeyStale4, 800);
  prefs.end();
  return true;
}

bool NvsStore::saveSetupPersist(const SetupPersist& in) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  bool ok = true;
  ok &= prefs.putUChar(kKeyWizPhase, in.phase) > 0;
  ok &= prefs.putBool(kKeyBaroOk, in.baro_acquired) > 0;
  ok &= prefs.putFloat(kKeyBaroKpa, in.baro_kpa) > 0;
  ok &= prefs.putUShort(kKeyStale0, in.stale_ms[0]) > 0;
  ok &= prefs.putUShort(kKeyStale1, in.stale_ms[1]) > 0;
  ok &= prefs.putUShort(kKeyStale2, in.stale_ms[2]) > 0;
  ok &= prefs.putUShort(kKeyStale3, in.stale_ms[3]) > 0;
  ok &= prefs.putUShort(kKeyStale4, in.stale_ms[4]) > 0;
  prefs.end();
  return ok;
}

bool NvsStore::loadEcuPersist(EcuPersist& out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  out.profile_id = prefs.getUChar(kKeyEcuProfile, 0);
  prefs.end();
  return true;
}

bool NvsStore::saveEcuPersist(const EcuPersist& in) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  const bool ok = prefs.putUChar(kKeyEcuProfile, in.profile_id) > 0;
  prefs.end();
  return ok;
}

bool NvsStore::loadUserSensors(UserSensorCfg (&out)[2], float& stoich_afr,
                               bool& afr_show_lambda) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  const bool has_schema = prefs.isKey(kKeyUsSchema);
  if (has_schema) {
    prefs.getUChar(kKeyUsSchema, 1);
    out[0].preset = static_cast<UserSensorPreset>(
        prefs.getUChar(kKeyUs0Preset, static_cast<uint8_t>(out[0].preset)));
    out[0].kind = static_cast<UserSensorKind>(
        prefs.getUChar(kKeyUs0Kind, static_cast<uint8_t>(out[0].kind)));
    out[0].source = static_cast<UserSensorSource>(
        prefs.getUChar(kKeyUs0Source, static_cast<uint8_t>(out[0].source)));
    out[0].scale = prefs.getFloat(kKeyUs0Scale, out[0].scale);
    out[0].offset = prefs.getFloat(kKeyUs0Offset, out[0].offset);
    String lbl0 = prefs.getString(kKeyUs0Label, out[0].label);
    strlcpy(out[0].label, lbl0.c_str(), sizeof(out[0].label));
    String um0 = prefs.getString(kKeyUs0UnitMetric, out[0].unit_metric);
    strlcpy(out[0].unit_metric, um0.c_str(), sizeof(out[0].unit_metric));
    String ui0 = prefs.getString(kKeyUs0UnitImperial, out[0].unit_imperial);
    strlcpy(out[0].unit_imperial, ui0.c_str(), sizeof(out[0].unit_imperial));

    out[1].preset = static_cast<UserSensorPreset>(
        prefs.getUChar(kKeyUs1Preset, static_cast<uint8_t>(out[1].preset)));
    out[1].kind = static_cast<UserSensorKind>(
        prefs.getUChar(kKeyUs1Kind, static_cast<uint8_t>(out[1].kind)));
    out[1].source = static_cast<UserSensorSource>(
        prefs.getUChar(kKeyUs1Source, static_cast<uint8_t>(out[1].source)));
    out[1].scale = prefs.getFloat(kKeyUs1Scale, out[1].scale);
    out[1].offset = prefs.getFloat(kKeyUs1Offset, out[1].offset);
    String lbl1 = prefs.getString(kKeyUs1Label, out[1].label);
    strlcpy(out[1].label, lbl1.c_str(), sizeof(out[1].label));
    String um1 = prefs.getString(kKeyUs1UnitMetric, out[1].unit_metric);
    strlcpy(out[1].unit_metric, um1.c_str(), sizeof(out[1].unit_metric));
    String ui1 = prefs.getString(kKeyUs1UnitImperial, out[1].unit_imperial);
    strlcpy(out[1].unit_imperial, ui1.c_str(), sizeof(out[1].unit_imperial));

    stoich_afr = prefs.getFloat(kKeyStoichAfr, stoich_afr);
    afr_show_lambda = prefs.getBool(kKeyAfrLambda, afr_show_lambda);
    prefs.end();
    return true;
  }
  prefs.end();

  // Migration from OilPersist if available.
  OilPersist oil{};
  if (loadOilPersist(oil)) {
    out[0] = UserSensorCfg{};
    out[0].preset = UserSensorPreset::kOilPressure;
    out[0].kind = UserSensorKind::kPressure;
    out[0].source =
        oil.swap ? UserSensorSource::kSensor2 : UserSensorSource::kSensor1;
    strlcpy(out[0].label, oil.label_p, sizeof(out[0].label));
    out[0].scale = 1.0f;
    out[0].offset = 0.0f;

    out[1] = UserSensorCfg{};
    out[1].preset = UserSensorPreset::kOilTemp;
    out[1].kind = UserSensorKind::kTemp;
    out[1].source =
        oil.swap ? UserSensorSource::kSensor1 : UserSensorSource::kSensor2;
    strlcpy(out[1].label, oil.label_t, sizeof(out[1].label));
    out[1].scale = 1.0f;
    out[1].offset = 0.0f;

    stoich_afr = 14.7f;
    afr_show_lambda = false;
    saveUserSensors(out, stoich_afr, afr_show_lambda);
    return true;
  }
  return false;
}

bool NvsStore::saveUserSensors(const UserSensorCfg (&in)[2], float stoich_afr,
                               bool afr_show_lambda) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  bool ok = true;
  ok &= PutUCharChecked(prefs, kKeyUsSchema, 1);
  ok &= PutUCharChecked(prefs, kKeyUs0Preset, static_cast<uint8_t>(in[0].preset));
  ok &= PutUCharChecked(prefs, kKeyUs0Kind, static_cast<uint8_t>(in[0].kind));
  ok &= PutUCharChecked(prefs, kKeyUs0Source, static_cast<uint8_t>(in[0].source));
  ok &= PutFloatChecked(prefs, kKeyUs0Scale, in[0].scale);
  ok &= PutFloatChecked(prefs, kKeyUs0Offset, in[0].offset);
  ok &= PutStringChecked(prefs, kKeyUs0Label, in[0].label);
  ok &= PutStringChecked(prefs, kKeyUs0UnitMetric, in[0].unit_metric);
  ok &= PutStringChecked(prefs, kKeyUs0UnitImperial, in[0].unit_imperial);
  ok &= PutUCharChecked(prefs, kKeyUs1Preset, static_cast<uint8_t>(in[1].preset));
  ok &= PutUCharChecked(prefs, kKeyUs1Kind, static_cast<uint8_t>(in[1].kind));
  ok &= PutUCharChecked(prefs, kKeyUs1Source, static_cast<uint8_t>(in[1].source));
  ok &= PutFloatChecked(prefs, kKeyUs1Scale, in[1].scale);
  ok &= PutFloatChecked(prefs, kKeyUs1Offset, in[1].offset);
  ok &= PutStringChecked(prefs, kKeyUs1Label, in[1].label);
  ok &= PutStringChecked(prefs, kKeyUs1UnitMetric, in[1].unit_metric);
  ok &= PutStringChecked(prefs, kKeyUs1UnitImperial, in[1].unit_imperial);
  ok &= PutFloatChecked(prefs, kKeyStoichAfr, stoich_afr);
  ok &= PutBoolChecked(prefs, kKeyAfrLambda, afr_show_lambda);
  prefs.end();
  return ok;
}

bool NvsStore::factoryResetClearAll() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  prefs.clear();
  prefs.end();
  return true;
}

bool NvsStore::loadWifiApPass(char* out, size_t out_len) {
  if (!out || out_len == 0) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  if (!prefs.isKey(kKeyWifiApPass)) {
    prefs.end();
    return false;
  }
  String val = prefs.getString(kKeyWifiApPass, "");
  prefs.end();
  if (val.length() == 0 || static_cast<size_t>(val.length()) >= out_len) {
    return false;
  }
  strlcpy(out, val.c_str(), out_len);
  return true;
}

bool NvsStore::saveWifiApPass(const char* pass) {
  if (!pass) return false;
  const size_t len = strlen(pass);
  if (len == 0 || len > 16) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  const bool ok = PutStringChecked(prefs, kKeyWifiApPass, pass);
  prefs.end();
  return ok;
}

bool NvsStore::clearWifiApPass() {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  const bool removed = prefs.remove(kKeyWifiApPass);
  prefs.end();
  return removed;
}
