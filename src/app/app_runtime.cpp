#include <Arduino.h>
#include <Wire.h>
#include <cstring>
#include <cmath>

#include "alerts/alerts_engine.h"
#include "app/app_globals.h"
#include "app/app_runtime.h"
#include "app/app_sleep.h"
#include "app_config.h"
#include "app_state.h"
#include "app/baro_auto.h"
#include "boot_ui.h"
#include "boot/boot_strings.h"
#include "can_link/twai_link.h"
#include "config/factory_config.h"
#include "config/logging.h"
#include "data/datastore.h"
#include "drivers/oled_u8g2.h"
#include "ecu/ecu_manager.h"
#include "app/can_runtime.h"
#include "app/input_runtime.h"
#include "app/button_task.h"
#include "app/display_init.h"
#include "app/can_bootstrap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "app/persist_runtime.h"
#include "ms3_decode/ms3_decode.h"
#include "pins.h"
#include "settings/nvs_store.h"
#include "can_link/can_autobaud.h"
#include "can_rx.h"
#include <WiFi.h>
#include "freertos/portmacro.h"
#include "app/can_recovery_eval.h"
#if SETUP_WIZARD_ENABLED
#include "setup_wizard/setup_wizard.h"
#endif
#include "ui_render.h"
#include "ui/pages.h"
#include "ui/edit_mode_helpers.h"
#include "wifi/wifi_portal.h"
#include "wifi/wifi_diag.h"
#ifdef UNIT_TEST
bool TestFailsafeShouldRestart(uint32_t pressed_since_ms, uint32_t now_ms,
                               uint8_t* click_count) {
  const bool restart =
      kFailsafeRestartHoldMs > 0 &&
      (now_ms - pressed_since_ms) >= kFailsafeRestartHoldMs;
  if (restart && click_count) {
    *click_count = 0;
  }
  return restart;
}
#endif

DataStore& ActiveStore() {
  return g_state.demo_mode ? g_datastore_demo : g_datastore_can;
}

void ResetBaroPersist() {
  SetupPersist persist{};
  g_nvs.loadSetupPersist(persist);
  persist.baro_acquired = false;
  persist.baro_kpa = 0.0f;
  g_nvs.saveSetupPersist(persist);
  g_state.baro_acquired = false;
  g_state.baro_kpa = AppConfig::kDefaultBaroKpa;
  ResetBaroAuto();
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    g_state.force_redraw[z] = true;
  }
}

void applySelfTestStep(AppState& state, uint8_t step) {
  state.self_test.request_reset_all = false;
  switch (step) {
    case 0:
      state.mock_data.speed_tenths = 0;
      state.mock_data.rpm = 0;
      state.mock_data.coolant_c = 0;
      state.mock_data.batt_mv = 0;
      state.mock_data.speed_valid = true;
      state.mock_data.rpm_valid = true;
      state.mock_data.coolant_valid = true;
      state.mock_data.batt_valid = true;
      break;
    case 1:
      state.mock_data.speed_tenths = 9;
      state.mock_data.rpm = 9;
      state.mock_data.coolant_c = 9;
      state.mock_data.batt_mv = 9000;
      state.mock_data.speed_valid = true;
      state.mock_data.rpm_valid = true;
      state.mock_data.coolant_valid = true;
      state.mock_data.batt_valid = true;
      break;
    case 2:
      state.mock_data.speed_tenths = 99;
      state.mock_data.rpm = 99;
      state.mock_data.coolant_c = 99;
      state.mock_data.batt_mv = 9900;
      state.mock_data.speed_valid = true;
      state.mock_data.rpm_valid = true;
      state.mock_data.coolant_valid = true;
      state.mock_data.batt_valid = true;
      break;
    case 3:
      state.mock_data.speed_tenths = 999;
      state.mock_data.rpm = 999;
      state.mock_data.coolant_c = 99;  // keep temp plausible
      state.mock_data.batt_mv = 9990;
      state.mock_data.speed_valid = true;
      state.mock_data.rpm_valid = true;
      state.mock_data.coolant_valid = true;
      state.mock_data.batt_valid = true;
      break;
    case 4:
      state.mock_data.speed_tenths = 1000;
      state.mock_data.rpm = 5000;
      state.mock_data.coolant_c = 104;
      state.mock_data.batt_mv = 12700;
      state.mock_data.speed_valid = true;
      state.mock_data.rpm_valid = true;
      state.mock_data.coolant_valid = true;
      state.mock_data.batt_valid = true;
      break;
    case 5:
      state.mock_data.speed_valid = false;
      state.mock_data.rpm_valid = false;
      state.mock_data.coolant_valid = false;
      state.mock_data.batt_valid = false;
      break;
    case 6:
    default:
      state.mock_data.speed_tenths = 321;
      state.mock_data.rpm = 3210;
      state.mock_data.coolant_c = 80;
      state.mock_data.batt_mv = 12300;
      state.mock_data.speed_valid = true;
      state.mock_data.rpm_valid = true;
      state.mock_data.coolant_valid = true;
      state.mock_data.batt_valid = true;
      state.self_test.request_reset_all = true;
      break;
  }
}

