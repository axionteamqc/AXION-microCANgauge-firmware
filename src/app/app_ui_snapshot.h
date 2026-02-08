#pragma once

#include <stdint.h>

#include "app_state.h"

struct AppUiSnapshot {
  bool wifi_mode_active = false;
  bool demo_mode = false;
  bool ui_locked = false;
  bool oled_primary_ready = false;
  bool oled_secondary_ready = false;
  uint8_t display_topology = 0;
  uint8_t focus_zone = 0;
  uint8_t page_index[kMaxZones] = {};
  uint8_t boot_page_index[kMaxZones] = {};
  bool screen_flip[kMaxZones] = {};
  bool baro_acquired = false;
  float baro_kpa = 0.0f;
  float page_recorded_min[kPageCount] = {};
  float page_recorded_max[kPageCount] = {};
  Thresholds thresholds[kPageCount] = {};
  uint32_t page_units_mask = 0;
  uint32_t page_alert_max_mask = 0;
  uint32_t page_alert_min_mask = 0;
  uint32_t page_hidden_mask = 0;
  uint32_t can_bitrate_value = 0;
  bool can_bitrate_locked = false;
  bool can_ready = false;
  bool can_safe_listen = false;
  CanHealth can_health = CanHealth::kNoFrames;
  UserSensorCfg user_sensor[2] = {};
  float stoich_afr = 0.0f;
  bool afr_show_lambda = false;
  char ecu_type[8] = "";
};

void GetAppUiSnapshot(AppUiSnapshot& out);
