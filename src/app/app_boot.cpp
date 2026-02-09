#include <Arduino.h>
#include <Wire.h>
#include <cstring>
#include <cmath>

#include "alerts/alerts_engine.h"
#include "app/app_globals.h"
#include "app/app_runtime.h"
#include "app/app_sleep.h"
#include "app/i2c_oled_log.h"
#include "app_config.h"
#include "app_state.h"
#include "app/page_mask.h"
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

void applySelfTestStep(AppState& state, uint8_t step);

#if SETUP_WIZARD_ENABLED
bool g_auto_setup_pending = false;
#endif

bool g_i2c_seen = false;
uint8_t g_i2c_scan_attempts = 0;
uint32_t g_last_i2c_scan_ms = 0;

volatile uint32_t g_can_rx_edge_count = 0;

static void IRAM_ATTR CanRxEdgeIsr() {
  ++g_can_rx_edge_count;
}

void AppSetup() {
  initDefaults(g_state);
  pinMode(Pins::kCanTx, INPUT_PULLUP);  // keep TX recessive ASAP
  pinMode(Pins::kCanRx, INPUT);
  attachInterrupt(digitalPinToInterrupt(Pins::kCanRx), CanRxEdgeIsr, CHANGE);

  Serial.begin(115200);
  LOGI("FW BUILD: %s %s\r\n", __DATE__, __TIME__);
  WifiDiagInit();
  g_i2c_seen = false;
  if (kEnableBootI2cScan) {
    if (kBootI2cPreScanDelayMs > 0) {
      AppSleepMs(kBootI2cPreScanDelayMs);
    }
    g_i2c_seen = ScanI2cBuses();
  }
  g_i2c_scan_attempts = 1;
  g_last_i2c_scan_ms = millis();
  if (AppConfig::kDecodeSelfTestEnabled) {
    RunMs3DecodeGoldenTest(g_decoder);
  }
  // Release: force Megasquirt profile.
  if (!g_ecu_mgr.initFromEcuType(g_state.ecu_type)) {
    g_ecu_mgr.initForcedMs3();
  }
  LOGI("ECU profile active: %s (ecu_type=%s)\r\n",
       g_ecu_mgr.activeName(),
       (g_state.ecu_type[0] != '\0') ? g_state.ecu_type : "(none)");

  StartButtonTask();

  const uint32_t boot_start_ms = millis();
  g_state.boot_ms = boot_start_ms;

  CanSettings can_cfg{};
  g_nvs.begin();
  bool cfg_pending = false;
  g_nvs.loadCfgPending(cfg_pending);
  if (cfg_pending) {
    LOGW("cfg_pending=true -> skipping NVS loads (defaults active)\r\n");
    BootStringsSet(kBootBrandText, kBootHelloLine1, kBootHelloLine2);
  } else {
    BootStringsInitFromNvs();
  }
  pinMode(Pins::kCanTx, INPUT_PULLUP);  // keep TX recessive before TWAI init
  if (!cfg_pending) {
    g_nvs.loadCanSettings(can_cfg);
    if (!can_cfg.hash_match && can_cfg.bitrate_locked) {
      LOGW("CAN lock cleared (DBC hash mismatch)\r\n");
    }
    g_state.can_bitrate_locked = can_cfg.bitrate_locked;
    g_state.can_bitrate_value = can_cfg.bitrate_value;
    g_state.id_present_mask = can_cfg.id_present_mask;
  }
#if SETUP_WIZARD_ENABLED
  g_auto_setup_pending = false;
#endif
  g_datastore_can.setDefaultStale(500);
  g_datastore_demo.setDefaultStale(500);
  SetupPersist setup_persist{};
  if (!cfg_pending && g_nvs.loadSetupPersist(setup_persist)) {
    const IEcuProfile& profile = g_ecu_mgr.profile();
    const uint8_t dash_count =
        std::min<uint8_t>(profile.dashIdCount(), static_cast<uint8_t>(5));
    for (uint8_t i = 0; i < dash_count; ++i) {
      SignalSpan span = profile.dashSignalsForIndex(i);
      g_datastore_can.setStaleForSignals(span.ids, span.count,
                                         setup_persist.stale_ms[i]);
      g_datastore_demo.setStaleForSignals(span.ids, span.count,
                                          setup_persist.stale_ms[i]);
    }
    g_state.baro_acquired = setup_persist.baro_acquired;
    if (setup_persist.baro_acquired) {
      g_state.baro_kpa = setup_persist.baro_kpa;
    }
  }
  UiPersist ui_p{};
  if (!cfg_pending) {
    g_nvs.loadUiPersist(ui_p);
    if (ui_p.has_display_topology) {
      g_state.display_topology =
          static_cast<DisplayTopology>(ui_p.display_topology);
    } else {
      g_state.display_topology = DisplayTopology::kSmallOnly;
    }
    g_state.demo_mode = ui_p.demo_mode;
    strlcpy(g_state.ecu_type, ui_p.ecu_type, sizeof(g_state.ecu_type));
    g_state.screen_cfg[0].flip_180 = ui_p.flip0;
    g_state.screen_cfg[1].flip_180 = ui_p.flip1;
    OilPersist oil_p{};
    g_nvs.loadOilPersist(oil_p);
    if (oil_p.mode != 1) {
      oil_p.mode = 1;  // force RAW; no UI to re-enable if left off
    }
    g_state.oil_cfg.mode =
        oil_p.mode == 1 ? AppState::OilConfig::Mode::kRaw
                        : AppState::OilConfig::Mode::kOff;
    g_state.oil_cfg.swap = oil_p.swap;
    strlcpy(g_state.oil_cfg.pressure_label, oil_p.label_p,
            sizeof(g_state.oil_cfg.pressure_label));
    strlcpy(g_state.oil_cfg.temp_label, oil_p.label_t,
            sizeof(g_state.oil_cfg.temp_label));
    {
      UserSensorCfg us[2] = {g_state.user_sensor[0], g_state.user_sensor[1]};
      float stoich = g_state.stoich_afr;
      bool show_lambda = g_state.afr_show_lambda;
      if (g_nvs.loadUserSensors(us, stoich, show_lambda)) {
        g_state.user_sensor[0] = us[0];
        g_state.user_sensor[1] = us[1];
        g_state.stoich_afr = stoich;
        g_state.afr_show_lambda = show_lambda;
      }
    }
    // Legacy compatibility: mirror user sensors into OilConfig for migration only.
    g_state.oil_cfg.swap =
        (g_state.user_sensor[0].source == UserSensorSource::kSensor2) &&
        (g_state.user_sensor[1].source == UserSensorSource::kSensor1);
    strlcpy(g_state.oil_cfg.pressure_label, g_state.user_sensor[0].label,
            sizeof(g_state.oil_cfg.pressure_label));
    strlcpy(g_state.oil_cfg.temp_label, g_state.user_sensor[1].label,
            sizeof(g_state.oil_cfg.temp_label));
    LOGI("OilPersist: loaded mode=%u -> state.mode=%u swap=%s demo=%s\r\n",
         static_cast<unsigned int>(oil_p.mode),
         static_cast<unsigned int>(g_state.oil_cfg.mode),
         g_state.oil_cfg.swap ? "true" : "false",
         g_state.demo_mode ? "true" : "false");
    // Restore pages via zones directly.
    auto clamp_page = [](uint8_t idx) -> uint8_t {
      return static_cast<uint8_t>(idx % kPageCount);
    };
    g_state.page_index[0] = clamp_page(ui_p.zone_page[0]);
    g_state.page_index[1] = clamp_page(ui_p.zone_page[1]);
    g_state.page_index[2] = clamp_page(ui_p.zone_page[2]);
    if (ui_p.has_boot_pages) {
      g_state.boot_page_index[0] = clamp_page(ui_p.boot_page[0]);
      g_state.boot_page_index[1] = clamp_page(ui_p.boot_page[1]);
      g_state.boot_page_index[2] = clamp_page(ui_p.boot_page[2]);
    } else {
      g_state.boot_page_index[0] = g_state.page_index[0];
      g_state.boot_page_index[1] = g_state.page_index[1];
      g_state.boot_page_index[2] = g_state.page_index[2];
      markUiDirty(millis());
    }
  }
  if (!cfg_pending) {
    // Boot page preferences are stored per zone (0..2). Do not remap indices here.
    // Large modes rely on zone 1 being independent from zone 2.
    // Apply boot pages at startup; runtime navigation remains in page_index.
    auto clamp_page = [](uint8_t idx) -> uint8_t {
      return static_cast<uint8_t>(idx % kPageCount);
    };
    g_state.page_index[0] = clamp_page(g_state.boot_page_index[0]);
    g_state.page_index[1] = clamp_page(g_state.boot_page_index[1]);
    g_state.page_index[2] = clamp_page(g_state.boot_page_index[2]);
    g_state.focus_zone = ui_p.focus % GetActiveZoneCount(g_state);
    g_state.page_units_mask = ui_p.page_units_mask;
    if (ui_p.has_page_hidden) {
      g_state.page_hidden_mask = ui_p.page_hidden_mask;
    } else {
      g_state.page_hidden_mask = 0;
    }
    EnsureVisiblePages(g_state);
    LOGI(
        "UI load boot pages: has_boot=%s ui_p.boot=[%u,%u,%u] "
        "boot_page_index=[%u,%u,%u] page_index=[%u,%u,%u]\r\n",
        ui_p.has_boot_pages ? "true" : "false",
        static_cast<unsigned int>(ui_p.boot_page[0]),
        static_cast<unsigned int>(ui_p.boot_page[1]),
        static_cast<unsigned int>(ui_p.boot_page[2]),
        static_cast<unsigned int>(g_state.boot_page_index[0]),
        static_cast<unsigned int>(g_state.boot_page_index[1]),
        static_cast<unsigned int>(g_state.boot_page_index[2]),
        static_cast<unsigned int>(g_state.page_index[0]),
        static_cast<unsigned int>(g_state.page_index[1]),
        static_cast<unsigned int>(g_state.page_index[2]));
    {
      auto zoneActive = [&](uint8_t z) -> bool {
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
      LOGI("UI load topo=%u active zones: Z0=%s Z1=%s Z2=%s\r\n",
           static_cast<unsigned int>(g_state.display_topology),
           zoneActive(0) ? "yes" : "no", zoneActive(1) ? "yes" : "no",
           zoneActive(2) ? "yes" : "no");
      (void)zoneActive;
    }
    if (!ui_p.has_page_units) {
      g_state.page_units_mask = PageMaskAll(kPageCount) &
                                0;  // defaults metric
      markUiDirty(millis());
    }
    if (ui_p.has_alert_masks) {
      g_state.page_alert_max_mask = ui_p.page_alert_max_mask;
      g_state.page_alert_min_mask = ui_p.page_alert_min_mask;
    } else {
      g_state.page_alert_max_mask = 0;
      g_state.page_alert_min_mask = 0;
      markUiDirty(millis());
    }
  }
  float thr_min[kPageCount];
  float thr_max[kPageCount];
  if (!cfg_pending && g_nvs.loadThresholds(thr_min, thr_max, kPageCount)) {
    for (size_t i = 0; i < kPageCount; ++i) {
      g_state.thresholds[i].min = thr_min[i];
      g_state.thresholds[i].max = thr_max[i];
    }
  }

  // Step 1: I2C bring-up
  const uint32_t oled1_boot_hz = AppConfig::kBootI2cClockHz;
  const uint32_t oled2_boot_hz =
      AppConfig::kSafeCableI2cEnabled ? AppConfig::kSafeCableBootI2cHz
                                      : AppConfig::kI2c2FrequencyHz;
  bool i2c1_ok = RecoverI2cBus(Pins::kI2cSda, Pins::kI2cScl, "OLED1",
                               kEnableVerboseSerialLogs);
  Wire.begin(Pins::kI2cSda, Pins::kI2cScl);
  g_wire_sda_pin = Pins::kI2cSda;
  g_wire_scl_pin = Pins::kI2cScl;
  const uint32_t i2c_begin_ms = millis();
  Wire.setClock(oled1_boot_hz);
  LOGI("OLED1 bus clock = %lu Hz (boot)\r\n",
       static_cast<unsigned long>(oled1_boot_hz));
  LOGI("OLED2 bus clock = %lu Hz (boot)\r\n",
       static_cast<unsigned long>(oled2_boot_hz));
#if defined(ESP32)
  Wire.setTimeOut(20);  // bound I2C stalls at boot; harmless if bus is healthy
#else
  // Wire.setTimeOut not available on this platform
#endif
  if (kBootI2cSettleDelayMs > 0) {
    delay(kBootI2cSettleDelayMs);
  }
  if (AppConfig::kOled2NoPullupsProtoFallback) {
    pinMode(Pins::kI2c2Sda, INPUT_PULLUP);
    pinMode(Pins::kI2c2Scl, INPUT_PULLUP);
  }
  showSoarerProgress(g_oled_primary, 25);
  if (kBootI2cPreOledDelayMs > 0) {
    delay(kBootI2cPreOledDelayMs);
  }

  // Step 2/3: OLED init per topology with graceful fallbacks
  bool i2c2_ok = RecoverI2cBus(Pins::kI2c2Sda, Pins::kI2c2Scl, "OLED2",
                               kEnableVerboseSerialLogs);
  auto probeAddr = []() {
    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() == 0) return true;
    Wire.beginTransmission(0x3D);
    return Wire.endTransmission() == 0;
  };
  auto initPrimary32 = [&]() {
    if (g_state.oled_primary_ready) return true;
    if (!i2c1_ok) return false;
    g_state.oled_primary_ready =
        initAndTestDisplay(g_oled_primary, true, oled1_boot_hz);
    if (g_state.oled_primary_ready) {
      g_oled_primary.setRotation(g_state.screen_cfg[0].flip_180);
      g_oled_primary.clearDisplay();
      I2cOledLogEvent(1, I2cOledAction::kClear, true, Pins::kI2cSda,
                      Pins::kI2cScl);
      g_oled_primary.sendRawCommand(0xAF);
    }
    return g_state.oled_primary_ready;
  };
  auto initPrimary64 = [&]() {
    if (g_state.oled_primary_ready) return true;
    if (!i2c1_ok) return false;
    g_state.oled_primary_ready =
        initAndTestDisplay64(g_oled_primary, true, oled1_boot_hz);
    if (g_state.oled_primary_ready) {
      g_oled_primary.setRotation(g_state.screen_cfg[0].flip_180);
      g_oled_primary.clearDisplay();
      I2cOledLogEvent(1, I2cOledAction::kClear, true, Pins::kI2cSda,
                      Pins::kI2cScl);
      g_oled_primary.sendRawCommand(0xAF);
    }
    return g_state.oled_primary_ready;
  };
  auto initSecondary32 = [&]() {
    if (g_state.oled_secondary_ready) return true;
    if (!i2c2_ok) return false;
    g_state.oled_secondary_ready =
        initAndTestDisplay(g_oled_secondary, false, oled2_boot_hz);
    if (g_state.oled_secondary_ready) {
      g_oled_secondary.setRotation(g_state.screen_cfg[1].flip_180);
      g_oled_secondary.clearDisplay();
      I2cOledLogEvent(2, I2cOledAction::kClear, true, Pins::kI2c2Sda,
                      Pins::kI2c2Scl);
      g_oled_secondary.sendRawCommand(0xAF);
    }
    return g_state.oled_secondary_ready;
  };

  auto initForTopology = [&]() {
    if (g_state.display_topology == DisplayTopology::kUnconfigured) {
      if (probeAddr()) {
        if (!initPrimary64()) {
          LOGW("OLED1 128x64 boot-safe init failed, will try 128x32\r\n");
          initPrimary32();
        }
      } else {
        LOGE("OLED1 not detected (0x3C/0x3D), continuing without display\r\n");
      }
    }

    switch (g_state.display_topology) {
      case DisplayTopology::kSmallOnly:
        initPrimary32();
        g_state.oled_secondary_ready = false;
        break;
      case DisplayTopology::kDualSmall:
        initPrimary32();
        initSecondary32();
        if (!g_state.oled_secondary_ready) {
          LOGW("OLED2 MISSING, fallback to SmallOnly\r\n");
          g_state.display_topology = DisplayTopology::kSmallOnly;
          g_state.oled_secondary_ready = false;
        }
        break;
      case DisplayTopology::kLargeOnly: {
        bool primary_ok = initPrimary64();
        if (!primary_ok) {
          initPrimary32();
          initSecondary32();
          if (g_state.oled_secondary_ready) {
            LOGW("OLED1 missing, fallback to DualSmall\r\n");
            g_state.display_topology = DisplayTopology::kDualSmall;
          } else {
            LOGE("NO OLED detected\r\n");
          }
        }
        g_state.oled_secondary_ready = false;
        break;
      }
      case DisplayTopology::kLargePlusSmall: {
        bool primary_ok = initPrimary64();
        bool secondary_ok = initSecondary32();
        if (!primary_ok) {
          if (secondary_ok) {
            LOGW("OLED1 missing, fallback to DualSmall\r\n");
            g_state.display_topology = DisplayTopology::kDualSmall;
          } else {
            LOGE("NO OLED detected\r\n");
          }
        } else if (!secondary_ok) {
          LOGW("OLED2 MISSING, fallback to LargeOnly\r\n");
          g_state.display_topology = DisplayTopology::kLargeOnly;
        }
        break;
      }
      case DisplayTopology::kUnconfigured:
      default:
        break;
    }
    const bool topo_uses_secondary =
        g_state.display_topology == DisplayTopology::kDualSmall ||
        g_state.display_topology == DisplayTopology::kLargePlusSmall;
    if (!topo_uses_secondary) {
      g_state.oled_secondary_ready = false;
    }
  };

  initForTopology();
  // Late OLED retries (non-blocking schedule based on millis).
  {
    const uint32_t kRetryOffsetsMs[] = {200, 500, 1000, 2000};
    const size_t kRetryCount = sizeof(kRetryOffsetsMs) / sizeof(kRetryOffsetsMs[0]);
    size_t retry_idx = 0;
    while (retry_idx < kRetryCount) {
      const bool need_secondary =
          (g_state.display_topology == DisplayTopology::kDualSmall ||
           g_state.display_topology == DisplayTopology::kLargePlusSmall);
      if (g_state.oled_primary_ready &&
          (g_state.oled_secondary_ready || !need_secondary)) {
        break;  // OLEDs confirmed (init + clearDisplay).
      }
      const uint32_t now_ms = millis();
      const uint32_t due_ms = i2c_begin_ms + kRetryOffsetsMs[retry_idx];
      if (static_cast<int32_t>(now_ms - due_ms) < 0) {
        AppSleepMs(1);
        continue;
      }
      if (!g_state.oled_primary_ready) {
        i2c1_ok = RecoverI2cBus(Pins::kI2cSda, Pins::kI2cScl, "OLED1",
                                kEnableVerboseSerialLogs);
      }
      if (!g_state.oled_secondary_ready) {
        i2c2_ok = RecoverI2cBus(Pins::kI2c2Sda, Pins::kI2c2Scl, "OLED2",
                                kEnableVerboseSerialLogs);
      }
      if (kEnableVerboseSerialLogs) {
        LOGW("OLED late retry %u/%u\r\n",
             static_cast<unsigned>(retry_idx + 1),
             static_cast<unsigned>(kRetryCount));
      }
      // Retry missing displays without changing topology.
      switch (g_state.display_topology) {
        case DisplayTopology::kSmallOnly:
          initPrimary32();
          break;
        case DisplayTopology::kDualSmall:
          initPrimary32();
          initSecondary32();
          break;
        case DisplayTopology::kLargeOnly:
          initPrimary64();
          break;
        case DisplayTopology::kLargePlusSmall:
          initPrimary64();
          initSecondary32();
          break;
        case DisplayTopology::kUnconfigured:
        default:
          if (probeAddr()) {
            if (!initPrimary64()) {
              initPrimary32();
            }
          }
          break;
      }
      ++retry_idx;
    }
  }
  showSoarerProgress(g_oled_primary, 75);

  // Step 4: finalize UI state
  g_state.dual_screens =
      g_state.oled_primary_ready && g_state.oled_secondary_ready;
  g_i2c_seen = g_state.oled_primary_ready || g_state.oled_secondary_ready;
  if (g_i2c_seen) {
    g_i2c_scan_attempts = 255;
  }
  g_state.focus_screen = g_state.focus_zone % GetActiveZoneCount(g_state);
  const bool has_page_cfg =
      !cfg_pending && (ui_p.has_boot_pages || ui_p.has_page0 || ui_p.has_page1);
  if (!has_page_cfg) {
    if (g_state.dual_screens) {
      g_state.page_index[0] = 0;
      g_state.page_index[1] = 1;
    } else {
      g_state.page_index[0] = 0;
      g_state.page_index[1] = 0;
    }
  }
  if (!cfg_pending) {
    if (!ui_p.has_boot_pages && ui_p.has_page0) {
      size_t pc = 0;
      GetPageTable(pc);
      g_state.page_index[0] = ui_p.page0 % pc;
    }
    if (!ui_p.has_boot_pages && g_state.dual_screens && ui_p.has_page1) {
      size_t pc = 0;
      GetPageTable(pc);
      g_state.page_index[1] = ui_p.page1 % pc;
    }
    if (!ui_p.has_boot_pages && g_state.dual_screens && ui_p.has_focus) {
      g_state.focus_screen = ui_p.focus ? 1 : 0;
    }
    // Migration: if no per-page units persisted but legacy screen units were set,
    // seed all pages from units0 once.
    if (!ui_p.has_page_units && (ui_p.units0 || ui_p.units1)) {
      const uint32_t all_pages_mask = PageMaskAll(kPageCount);
      g_state.page_units_mask = ui_p.units0 ? all_pages_mask : 0;
      markUiDirty(millis());
    }
  }
  showSoarerProgress(g_oled_primary, 100);

  if (AppConfig::IsRealCanEnabled()) {
    if (g_state.can_bitrate_locked) {
      if (!g_twai.isStarted()) {
        if (g_twai.startNormal(g_state.can_bitrate_value)) {
          g_state.can_ready = true;
          LOGI("CAN started @%lu normal\r\n",
               static_cast<unsigned long>(g_state.can_bitrate_value));
        } else {
          LOGE("CAN start failed\r\n");
        }
      } else {
        g_state.can_ready = true;
      }
    } else {
      uint32_t rate = g_state.can_bitrate_value;
      if (rate == 0) {
        rate = g_ecu_mgr.profile().preferredBitrate();
        g_state.can_bitrate_value = rate;
      }
      if (!g_twai.isStarted()) {
        if (g_twai.startNormal(rate)) {
          g_state.can_ready = true;
          LOGI("CAN started @%lu (unlocked)\r\n",
               static_cast<unsigned long>(rate));
        } else {
          LOGE("CAN start failed (unlocked)\r\n");
        }
      } else {
        g_state.can_ready = true;
      }
    }
  } else {
    LOGI("CAN mock mode (real CAN disabled)\r\n");
  }

  const uint32_t elapsed_boot = millis() - boot_start_ms;
  if (elapsed_boot < 2000) {
    delay(2000 - elapsed_boot);
  }

  playHelloSequence(g_oled_primary);
  flushButtonQueue();
  g_state.force_redraw[0] = true;
  g_state.force_redraw[1] = true;
  g_state.last_input_ms = millis();

  g_state.self_test_enabled = AppConfig::kUiSelfTestEnabled;
  if (g_state.self_test_enabled) {
    g_state.self_test.last_step_ms = millis();
    applySelfTestStep(g_state, g_state.self_test.step);
  }
  StartCanRxTask();
}
