#include "wifi/wifi_portal_apply_internal.h"

// THREADING CONTRACT (g_state):
// - Writer: portal Apply commits g_state under g_state_mux (short critical section).
// - Readers: UI/CAN tasks may read concurrently; use snapshots or lock when cross-task.
// - Non-shared parsing/persistence must stay outside the lock.

#include <Arduino.h>
#include <cstring>

#include "app/app_globals.h"
#include "app_state.h"
#include "boot/boot_strings.h"
#include "config/logging.h"
#include "settings/nvs_store.h"
#include "settings/ui_persist_build.h"
#include "wifi/wifi_ap_pass.h"

bool ApplyCommitAndPersist(const ApplyCommitData& data,
                           const String& brand_str,
                           const String& hello1_str,
                           const String& hello2_str) {
  UserSensorCfg us_cfg[2] = {data.user_sensor[0], data.user_sensor[1]};
  if (data.oil_swap) {
    us_cfg[0].source = UserSensorSource::kSensor2;
    us_cfg[1].source = UserSensorSource::kSensor1;
  }
  portENTER_CRITICAL(&g_state_mux);
  g_state.display_topology = data.topo;
  g_state.screen_cfg[0].flip_180 = data.flip0;
  g_state.screen_cfg[1].flip_180 = data.flip1;
  g_state.user_sensor[0] = us_cfg[0];
  g_state.user_sensor[1] = us_cfg[1];
  g_state.stoich_afr = data.stoich_afr;
  g_state.afr_show_lambda = data.afr_show_lambda;
  strlcpy(g_state.oil_cfg.pressure_label, g_state.user_sensor[0].label,
          sizeof(g_state.oil_cfg.pressure_label));
  strlcpy(g_state.oil_cfg.temp_label, g_state.user_sensor[1].label,
          sizeof(g_state.oil_cfg.temp_label));
  g_state.oil_cfg.swap =
      (g_state.user_sensor[0].source == UserSensorSource::kSensor2) &&
      (g_state.user_sensor[1].source == UserSensorSource::kSensor1);
  g_state.demo_mode = data.demo_mode;
  if (data.ecu_type_valid) {
    strlcpy(g_state.ecu_type, data.ecu_type, sizeof(g_state.ecu_type));
  }
  g_state.boot_page_index[0] = data.boot_pages_internal[0];
  g_state.boot_page_index[1] = data.boot_pages_internal[1];
  g_state.boot_page_index[2] = data.boot_pages_internal[2];
  auto zone_active = [&](uint8_t z) -> bool {
    switch (g_state.display_topology) {
      case DisplayTopology::kSmallOnly:
        return z == 0;
      case DisplayTopology::kDualSmall:
        if (z == 0) return true;
        if (z == 2) return g_state.dual_screens;
        return false;
      case DisplayTopology::kLargeOnly:
        return z == 0 || z == 1;
      case DisplayTopology::kLargePlusSmall:
        if (z == 0 || z == 1) return true;
        if (z == 2) return g_state.dual_screens;
        return false;
      case DisplayTopology::kUnconfigured:
      default:
        return z == 0;
    }
  };
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    if (zone_active(z)) {
      g_state.page_index[z] = g_state.boot_page_index[z];
    }
  }
  g_state.can_bitrate_value = data.can_bitrate_value;
  g_state.can_bitrate_locked = data.can_bitrate_locked;
  g_state.can_ready = data.can_ready;
  g_state.id_present_mask = data.id_present_mask;
  g_state.page_units_mask = data.page_units_mask;
  g_state.page_alert_max_mask = data.page_alert_max_mask;
  g_state.page_alert_min_mask = data.page_alert_min_mask;
  g_state.page_hidden_mask = data.page_hidden_mask;
  memcpy(g_state.thresholds, data.thresholds, sizeof(data.thresholds));
  EnsureVisiblePages(g_state);
  portEXIT_CRITICAL(&g_state_mux);

  UiPersist ui = BuildUiPersistFromState(g_state);
  ui.display_topology = static_cast<uint8_t>(data.topo);
  ui.flip0 = data.flip0;
  ui.flip1 = data.flip1;
  ui.page_units_mask = data.page_units_mask;
  ui.page_alert_max_mask = data.page_alert_max_mask;
  ui.page_alert_min_mask = data.page_alert_min_mask;
  ui.page_hidden_mask = data.page_hidden_mask;
  if (!g_nvs.setCfgPending(true)) {
    LOGE("NVS setCfgPending(true) failed\r\n");
    return false;
  }
  bool ok = true;
  if (!g_nvs.saveUserSensors(g_state.user_sensor, g_state.stoich_afr,
                             g_state.afr_show_lambda)) {
    LOGE("NVS saveUserSensors failed\r\n");
    ok = false;
  }
  CanSettings can_cfg{};
  can_cfg.bitrate_locked = data.can_bitrate_locked;
  can_cfg.bitrate_value = data.can_bitrate_value;
  can_cfg.id_present_mask = data.id_present_mask;
  can_cfg.hash_match = true;
  if (!g_nvs.saveCanSettings(can_cfg)) {
    LOGE("NVS saveCanSettings failed\r\n");
    ok = false;
  }
  if (!g_nvs.saveUiPersist(ui)) {
    LOGE("NVS saveUiPersist failed\r\n");
    ok = false;
  }
  // Persist oil swap (keep existing mode/labels).
  OilPersist oil_p{};
  oil_p.mode = static_cast<uint8_t>(g_state.oil_cfg.mode);
  oil_p.swap = g_state.oil_cfg.swap;
  strlcpy(oil_p.label_p, g_state.user_sensor[0].label, sizeof(oil_p.label_p));
  strlcpy(oil_p.label_t, g_state.user_sensor[1].label, sizeof(oil_p.label_t));
  if (!g_nvs.saveOilPersist(oil_p)) {
    LOGE("NVS saveOilPersist failed\r\n");
    ok = false;
  }
  // Persist thresholds separately.
  float mins[kPageCount];
  float maxs[kPageCount];
  for (size_t i = 0; i < kPageCount; ++i) {
    mins[i] = g_state.thresholds[i].min;
    maxs[i] = g_state.thresholds[i].max;
  }
  if (!g_nvs.saveThresholds(mins, maxs, kPageCount)) {
    LOGE("NVS saveThresholds failed\r\n");
    ok = false;
  }
  if (!g_nvs.saveBootTexts(brand_str, hello1_str, hello2_str)) {
    LOGE("NVS saveBootTexts failed\r\n");
    ok = false;
  }
  BootStringsSet(brand_str.c_str(), hello1_str.c_str(), hello2_str.c_str());
  if (ok) {
    if (!g_nvs.setCfgPending(false)) {
      LOGE("NVS setCfgPending(false) failed\r\n");
      ok = false;
    }
  }
  if (ok && data.wifi_ap_pw_valid) {
    if (!WifiApPassSetAndPersist(data.wifi_ap_pw)) {
      LOGE("NVS saveWifiApPass failed\r\n");
      ok = false;
      if (!g_nvs.setCfgPending(true)) {
        LOGE("NVS setCfgPending(true) failed\r\n");
      }
    }
  }
  return ok;
}
