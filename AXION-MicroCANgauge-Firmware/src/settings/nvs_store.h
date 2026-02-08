#pragma once

#include <Arduino.h>

#ifndef NVS_VERIFY_WRITES
#define NVS_VERIFY_WRITES 0
#endif

class Preferences;

#include "app_state.h"

struct CanSettings {
  uint8_t schema_version = 1;
  bool bitrate_locked = false;
  uint32_t bitrate_value = 500000;
  uint8_t id_present_mask = 0;
  bool hash_match = true;
  uint8_t ecu_profile_id = 1;  // 1 = MS3 by default
};

struct SetupPersist {
  enum class Phase : uint8_t { kNone = 0, kKoeoDone = 1, kRunDone = 2 };
  uint8_t phase = 0;
  bool baro_acquired = false;
  float baro_kpa = 0.0f;
  uint16_t stale_ms[5] = {0, 0, 0, 0, 0};
};

struct OilPersist {
  uint8_t mode = 1;  // 0=off,1=raw
  bool swap = false;
  char label_p[8] = "OilP";
  char label_t[8] = "OilT";
};

struct UiPersist {
  uint8_t page0 = 0;
  uint8_t page1 = 0;
  uint8_t zone_page[3] = {0, 0, 0};
  uint8_t boot_page[3] = {0, 0, 0};
  uint8_t focus = 0;
  uint8_t display_topology = 0;
  bool demo_mode = false;
  char ecu_type[8] = "MS3";
  bool units0 = false;
  bool units1 = false;
  bool flip0 = false;
  bool flip1 = false;
  bool has_page0 = false;
  bool has_page1 = false;
  bool has_focus = false;
  uint32_t page_units_mask = 0;
  bool has_page_units = false;
  bool has_zone_pages = false;
  bool has_boot_pages = false;
  uint32_t page_alert_max_mask = 0;
  uint32_t page_alert_min_mask = 0;
  uint32_t page_hidden_mask = 0;
  bool has_alert_masks = false;
  bool has_display_topology = false;
  bool has_page_hidden = false;
};

struct EcuPersist {
  uint8_t profile_id = 0;  // 0=AUTO, 1=MEGASQUIRT
};

class NvsStore {
 public:
  NvsStore();
  bool begin();
  bool loadCanSettings(CanSettings& out);
  bool saveCanSettings(const CanSettings& in);
  const char* dbcHash() const { return kDbcSha256; }

  bool loadSetupPersist(SetupPersist& out);
  bool saveSetupPersist(const SetupPersist& in);
  bool loadOilPersist(OilPersist& out);
  bool saveOilPersist(const OilPersist& in);
  bool loadThresholds(float* mins, float* maxs, size_t count);
  bool saveThresholds(const float* mins, const float* maxs, size_t count);
  bool loadUiPersist(UiPersist& out);
  bool saveUiPersist(const UiPersist& in);
  bool loadEcuPersist(EcuPersist& out);
  bool saveEcuPersist(const EcuPersist& in);
  bool loadCfgPending(bool& pending);
  bool setCfgPending(bool pending);
  bool loadBootTexts(String& brand, String& h1, String& h2);
  bool saveBootTexts(const String& brand, const String& h1, const String& h2);
  bool loadUserSensors(UserSensorCfg (&out)[2], float& stoich_afr,
                       bool& afr_show_lambda);
  bool saveUserSensors(const UserSensorCfg (&in)[2], float stoich_afr,
                       bool afr_show_lambda);
  bool factoryResetClearAll();
  bool loadWifiApPass(char* out, size_t out_len);
  bool saveWifiApPass(const char* pass);
  bool clearWifiApPass();

 private:
  bool ready_;
  static constexpr const char* kNamespace = "cangauge";
  static constexpr const char* kKeyLocked = "can_lock";
  static constexpr const char* kKeyBitrate = "can_rate";
  static constexpr const char* kKeyIdMask = "id_mask";
  static constexpr const char* kKeyHash = "dbc_hash";
  static constexpr const char* kKeyCanVer = "can_ver";
  static constexpr const char* kKeyCanProfile = "can_prof";
  static constexpr const char* kKeyWizPhase = "wiz_phase";
  static constexpr const char* kKeyBaroOk = "baro_ok";
  static constexpr const char* kKeyBaroKpa = "baro_kpa";
  static constexpr const char* kKeyStale0 = "stale0";
  static constexpr const char* kKeyStale1 = "stale1";
  static constexpr const char* kKeyStale2 = "stale2";
  static constexpr const char* kKeyStale3 = "stale3";
  static constexpr const char* kKeyStale4 = "stale4";
  static constexpr const char* kKeyThrMinPrefix = "thrmin";
  static constexpr const char* kKeyThrMaxPrefix = "thrmax";
  static constexpr const char* kKeyUiPage0 = "ui_pg0";
  static constexpr const char* kKeyUiPage1 = "ui_pg1";
  static constexpr const char* kKeyUiFocus = "ui_focus";
  static constexpr const char* kKeyUiUnits0 = "ui_u0";
  static constexpr const char* kKeyUiUnits1 = "ui_u1";
  static constexpr const char* kKeyUiFlip0 = "ui_f0";
  static constexpr const char* kKeyUiFlip1 = "ui_f1";
  static constexpr const char* kKeyUiAlertMax32 = "ui_am32";
  static constexpr const char* kKeyUiAlertMin32 = "ui_al32";
  static constexpr const char* kKeyUiHasZones = "ui_has_z";
  static constexpr const char* kKeyUiZone0 = "ui_z0";
  static constexpr const char* kKeyUiZone1 = "ui_z1";
  static constexpr const char* kKeyUiZone2 = "ui_z2";
  static constexpr const char* kKeyUiBoot0 = "ui_b0";
  static constexpr const char* kKeyUiBoot1 = "ui_b1";
  static constexpr const char* kKeyUiBoot2 = "ui_b2";
  static constexpr const char* kKeyUiPageUnits = "ui_pu";
  static constexpr const char* kKeyUiPageUnits32 = "ui_pu32";
  static constexpr const char* kKeyUiHidden = "ui_hid";
  static constexpr const char* kKeyUiHidden32 = "ui_hid32";
  static constexpr const char* kKeyUiDisplayTopo = "ui_dt";
  static constexpr const char* kKeyUiDemo = "ui_demo";
  static constexpr const char* kKeyUiEcu = "ui_ecu";
  static constexpr const char* kKeyEcuProfile = "ecu_prof";
  static constexpr const char* kKeyBootBrand = "boot_brand";
  static constexpr const char* kKeyBootHello1 = "boot_h1";
  static constexpr const char* kKeyBootHello2 = "boot_h2";
  static constexpr const char* kKeyCfgPending = "cfg_pending";
  static constexpr const char* kKeyUsSchema = "us_schema";
  static constexpr const char* kKeyUs0Preset = "us0_preset";
  static constexpr const char* kKeyUs0Kind = "us0_kind";
  static constexpr const char* kKeyUs0Source = "us0_src";
  static constexpr const char* kKeyUs0Scale = "us0_scl";
  static constexpr const char* kKeyUs0Offset = "us0_off";
  static constexpr const char* kKeyUs0Label = "us0_lbl";
  static constexpr const char* kKeyUs0UnitMetric = "us0_um";
  static constexpr const char* kKeyUs0UnitImperial = "us0_ui";
  static constexpr const char* kKeyUs1Preset = "us1_preset";
  static constexpr const char* kKeyUs1Kind = "us1_kind";
  static constexpr const char* kKeyUs1Source = "us1_src";
  static constexpr const char* kKeyUs1Scale = "us1_scl";
  static constexpr const char* kKeyUs1Offset = "us1_off";
  static constexpr const char* kKeyUs1Label = "us1_lbl";
  static constexpr const char* kKeyUs1UnitMetric = "us1_um";
  static constexpr const char* kKeyUs1UnitImperial = "us1_ui";
  static constexpr const char* kKeyStoichAfr = "stoich_afr";
  static constexpr const char* kKeyAfrLambda = "afr_lambda";
 static constexpr const char* kKeyWifiApPass = "wifi_ap_pw";
  static constexpr const char* kDbcSha256 =
      "791e994238cf0e79f6a100e9550e32f3b3399c8abf8b4ff22a36e90ffd6dc693";

  static bool PutUCharChecked(Preferences& prefs, const char* key, uint8_t value);
  static bool PutUShortChecked(Preferences& prefs, const char* key, uint16_t value);
  static bool PutUIntChecked(Preferences& prefs, const char* key, uint32_t value);
  static bool PutBoolChecked(Preferences& prefs, const char* key, bool value);
  static bool PutFloatChecked(Preferences& prefs, const char* key, float value);
  static bool PutStringChecked(Preferences& prefs, const char* key, const char* value);
};
