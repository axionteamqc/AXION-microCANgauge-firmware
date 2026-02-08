#include "app/app_ui_snapshot.h"

#include <cstring>

#include "app/app_globals.h"
#include "freertos/portmacro.h"

void GetAppUiSnapshot(AppUiSnapshot& out) {
  portENTER_CRITICAL(&g_state_mux);
  out.wifi_mode_active = g_state.wifi_mode_active;
  out.demo_mode = g_state.demo_mode;
  out.ui_locked = g_state.ui_locked;
  out.oled_primary_ready = g_state.oled_primary_ready;
  out.oled_secondary_ready = g_state.oled_secondary_ready;
  out.display_topology = static_cast<uint8_t>(g_state.display_topology);
  out.focus_zone = g_state.focus_zone;
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    out.page_index[z] = g_state.page_index[z];
    out.boot_page_index[z] = g_state.boot_page_index[z];
    out.screen_flip[z] = g_state.screen_cfg[z].flip_180;
  }
  out.baro_acquired = g_state.baro_acquired;
  out.baro_kpa = g_state.baro_kpa;
  for (size_t i = 0; i < kPageCount; ++i) {
    out.page_recorded_min[i] = g_state.page_recorded_min[i];
    out.page_recorded_max[i] = g_state.page_recorded_max[i];
    out.thresholds[i] = g_state.thresholds[i];
  }
  out.page_units_mask = g_state.page_units_mask;
  out.page_alert_max_mask = g_state.page_alert_max_mask;
  out.page_alert_min_mask = g_state.page_alert_min_mask;
  out.page_hidden_mask = g_state.page_hidden_mask;
  out.can_bitrate_value = g_state.can_bitrate_value;
  out.can_bitrate_locked = g_state.can_bitrate_locked;
  out.can_ready = g_state.can_ready;
  out.can_safe_listen = g_state.can_safe_listen;
  out.can_health = g_state.can_link.health;
  out.user_sensor[0] = g_state.user_sensor[0];
  out.user_sensor[1] = g_state.user_sensor[1];
  out.stoich_afr = g_state.stoich_afr;
  out.afr_show_lambda = g_state.afr_show_lambda;
  strlcpy(out.ecu_type, g_state.ecu_type, sizeof(out.ecu_type));
  portEXIT_CRITICAL(&g_state_mux);
}
