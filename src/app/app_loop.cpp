#include <Arduino.h>
#include <cmath>
#include <WiFi.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "alerts/alerts_engine.h"
#include "app/app_globals.h"
#include "app/app_runtime.h"
#include "app/app_sleep.h"
#include "app/i2c_oled_log.h"
#include "app_config.h"
#include "app_state.h"
#include "app/baro_auto.h"
#include "app/can_bootstrap.h"
#include "app/can_runtime.h"
#include "app/display_init.h"
#include "app/input_runtime.h"
#include "app/persist_runtime.h"
#include "config/factory_config.h"
#include "config/logging.h"
#include "data/datastore.h"
#include "drivers/oled_u8g2.h"
#include "freertos/portmacro.h"
#include "pins.h"
#include "ui_render.h"
#include "ui/pages.h"
#include "ui/edit_mode_helpers.h"
#include "wifi/wifi_portal.h"
#include "wifi/wifi_diag.h"
#include "wifi/wifi_portal_internal.h"
#if SETUP_WIZARD_ENABLED
#include "setup_wizard/setup_wizard.h"
#endif

void applySelfTestStep(AppState& state, uint8_t step);

#if SETUP_WIZARD_ENABLED
extern bool g_auto_setup_pending;
#endif
extern bool g_i2c_seen;
extern uint8_t g_i2c_scan_attempts;
extern uint32_t g_last_i2c_scan_ms;

static bool ProbeOled1Ack() {
  if (g_wire_sda_pin != Pins::kI2cSda || g_wire_scl_pin != Pins::kI2cScl) {
    LOGW("Wire not on OLED1 bus, rebinding\r\n");
    Wire.begin(Pins::kI2cSda, Pins::kI2cScl);
    g_wire_sda_pin = Pins::kI2cSda;
    g_wire_scl_pin = Pins::kI2cScl;
    uint32_t bus_hz = g_oled_primary.busClockHz();
    if (bus_hz == 0) {
      bus_hz = AppConfig::kBootI2cClockHz;
    }
    Wire.setClock(bus_hz);
    Wire.setTimeOut(20);
  }
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() == 0) return true;
  Wire.beginTransmission(0x3D);
  return Wire.endTransmission() == 0;
}

static bool MaybeResetWireOled1(uint32_t now_ms, uint32_t bus_hz,
                                bool log_verbose) {
  static uint32_t reset_window_ms = 0;
  static uint8_t reset_count = 0;
  if (reset_window_ms == 0 || (now_ms - reset_window_ms) > 60000U) {
    reset_window_ms = now_ms;
    reset_count = 0;
  }
  if (reset_count >= 3) {
    if (log_verbose) {
      LOGW("OLED1 I2C hard reset rate-limited\r\n");
    }
    I2cOledLogEvent(1, I2cOledAction::kWireReset, false, Pins::kI2cSda,
                    Pins::kI2cScl);
    return false;
  }
  ++reset_count;
  if (log_verbose) {
    LOGW("OLED1 I2C hard reset (Wire) #%u\r\n",
         static_cast<unsigned>(reset_count));
  }
  I2cOledLogEvent(1, I2cOledAction::kWireReset, true, Pins::kI2cSda,
                  Pins::kI2cScl);
  Wire.end();
  Wire.begin(Pins::kI2cSda, Pins::kI2cScl);
  g_wire_sda_pin = Pins::kI2cSda;
  g_wire_scl_pin = Pins::kI2cScl;
  Wire.setClock(bus_hz);
  Wire.setTimeOut(20);
  return true;
}

static void DemoTick(AppState& state, uint32_t now_ms) {
  state.mock_data.update(now_ms);
  state.demo_tick_count++;
  const float t = static_cast<float>(now_ms) / 1000.0f;
  const float rpm = 900.0f + 3200.0f * (0.5f * (sinf(t * 0.8f) + 1.0f));
  const float map_kpa = 40.0f + 60.0f * (0.5f * (sinf(t * 0.4f) + 1.0f));
  const float clt_c = 80.0f + 20.0f * (0.5f * (sinf(t * 0.1f) + 1.0f));
  const float mat_c = 55.0f + 15.0f * (0.5f * (sinf(t * 0.12f) + 1.0f));
  const float batt_v = 12.2f + 0.6f * (0.5f * (sinf(t * 0.2f) + 1.0f));
  const float tps = 5.0f + 80.0f * (0.5f * (sinf(t * 0.6f) + 1.0f));
  const float adv = 15.0f + 20.0f * (0.5f * (sinf(t * 0.35f) + 1.0f));
  const float afr = 12.0f + 3.0f * (0.5f * (sinf(t * 0.55f) + 1.0f));
  const float afr_tgt = 12.5f + 2.0f * (0.5f * (sinf(t * 0.45f) + 1.0f));
  const float knk = 1.0f + 2.5f * (0.5f * (sinf(t * 0.3f) + 1.0f));
  const float vss_ms = 10.0f + 20.0f * (0.5f * (sinf(t * 0.25f) + 1.0f));
  const float egt_f = 900.0f + 200.0f * (0.5f * (sinf(t * 0.22f) + 1.0f));
  const float sens1 = 20.0f + 5.0f * (0.5f * (sinf(t * 0.33f) + 1.0f));
  const float sens2 = 60.0f + 8.0f * (0.5f * (sinf(t * 0.28f) + 1.0f));
#if DEV_INJECT_INVALID || DEV_INJECT_STALE
  const uint32_t cycle_ms = 20000U;
  const uint32_t phase = now_ms % cycle_ms;
#if DEV_INJECT_INVALID
  const bool rpm_invalid_phase = (phase >= 2000U) && (phase < 5000U);
  const bool map_invalid_phase = (phase >= 12000U) && (phase < 15000U);
#else
  constexpr bool rpm_invalid_phase = false;
  constexpr bool map_invalid_phase = false;
#endif
#if DEV_INJECT_STALE
  const bool rpm_stale_phase = (phase >= 7000U) && (phase < 10000U);
  const bool map_stale_phase = (phase >= 15000U) && (phase < 18000U);
#else
  constexpr bool rpm_stale_phase = false;
  constexpr bool map_stale_phase = false;
#endif
#else
  constexpr bool rpm_invalid_phase = false;
  constexpr bool rpm_stale_phase = false;
  constexpr bool map_invalid_phase = false;
  constexpr bool map_stale_phase = false;
#endif

  state.baro_acquired = true;
  state.baro_kpa = AppConfig::kDefaultBaroKpa;

  if (!rpm_stale_phase) {
    g_datastore_demo.update(SignalId::kRpm, rpm, now_ms);
    if (rpm_invalid_phase) {
      g_datastore_demo.note_invalid(SignalId::kRpm, now_ms);
    }
  }
  if (!map_stale_phase) {
    g_datastore_demo.update(SignalId::kMap, map_kpa, now_ms);
    if (map_invalid_phase) {
      g_datastore_demo.note_invalid(SignalId::kMap, now_ms);
    }
  }
  g_datastore_demo.update(SignalId::kClt, clt_c * 9.0f / 5.0f + 32.0f, now_ms);
  g_datastore_demo.update(SignalId::kMat, mat_c * 9.0f / 5.0f + 32.0f, now_ms);
  g_datastore_demo.update(SignalId::kBatt, batt_v, now_ms);
  g_datastore_demo.update(SignalId::kTps, tps, now_ms);
  g_datastore_demo.update(SignalId::kAdv, adv, now_ms);
  g_datastore_demo.update(SignalId::kAfr1, afr, now_ms);
  g_datastore_demo.update(SignalId::kAfrTarget1, afr_tgt, now_ms);
  g_datastore_demo.update(SignalId::kKnkRetard, knk, now_ms);
  g_datastore_demo.update(SignalId::kVss1, vss_ms, now_ms);  // m/s
  g_datastore_demo.update(SignalId::kEgt1, egt_f, now_ms);   // F
  g_datastore_demo.update(SignalId::kSensors1, sens1, now_ms);
  g_datastore_demo.update(SignalId::kSensors2, sens2, now_ms);
  const float pw1 = 2.0f + 4.0f * (0.5f * (sinf(t * 0.42f) + 1.0f));   // 2..6 ms
  const float pw2 = 2.0f + 4.0f * (0.5f * (sinf(t * 0.50f + 1.0f) + 1.0f));  // phase décalée
  const float pwseq1 = 1.0f + 3.0f * (0.5f * (sinf(t * 0.38f + 0.6f) + 1.0f));  // 1..4 ms
  const float ego = 80.0f + 40.0f * (0.5f * (sinf(t * 0.33f + 0.2f) + 1.0f));   // 80..120 %
  const float launch = 0.0f + 15.0f * (0.5f * (sinf(t * 0.27f + 0.4f) + 1.0f)); // 0..15 deg
  const float tc = 0.0f + 10.0f * (0.5f * (sinf(t * 0.30f + 0.8f) + 1.0f));     // 0..10 deg
  g_datastore_demo.update(SignalId::kPw1, pw1, now_ms);
  g_datastore_demo.update(SignalId::kPw2, pw2, now_ms);
  g_datastore_demo.update(SignalId::kPwSeq1, pwseq1, now_ms);
  g_datastore_demo.update(SignalId::kEgoCor1, ego, now_ms);
  g_datastore_demo.update(SignalId::kLaunchTiming, launch, now_ms);
  g_datastore_demo.update(SignalId::kTcRetard, tc, now_ms);
}

static void MaybeTriggerUnlockedRescan(AppState& state, uint32_t now_ms) {
  if (kCanUnlockedRescanNoFramesMs == 0) return;
  if (state.can_bitrate_locked || !state.can_ready) return;
  if (state.can_link.health != CanHealth::kNoFrames) return;
  const uint32_t last_change = state.can_link.last_health_change_ms;
  if (last_change == 0) return;
  const uint32_t no_frames_ms = now_ms - last_change;
  if (no_frames_ms < kCanUnlockedRescanNoFramesMs) return;
  static uint32_t last_rescan_ms = 0;
  if (last_rescan_ms != 0 &&
      (now_ms - last_rescan_ms) < kCanUnlockedRescanNoFramesMs) {
    return;
  }
  portENTER_CRITICAL(&g_state_mux);
  state.can_ready = false;
  portEXIT_CRITICAL(&g_state_mux);
  last_rescan_ms = now_ms;
  if (kEnableVerboseSerialLogs) {
    LOGW("CAN NO_FRAMES %lums, rescan unlocked\r\n",
         static_cast<unsigned long>(no_frames_ms));
  }
}

static void updateRecordedExtrema(AppState& state, const DataStore& store,
                                  uint32_t now_ms) {
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  ScreenSettings cfg{};
  for (size_t i = 0; i < page_count; ++i) {
    cfg.imperial_units = GetPageUnits(state, static_cast<uint8_t>(i));
    cfg.flip_180 = false;
    float canon = 0.0f;
    if (PageCanonicalValue(pages[i].id, state, cfg, store, now_ms, canon)) {
      if (isnan(state.page_recorded_max[i]) || canon > state.page_recorded_max[i]) {
        state.page_recorded_max[i] = canon;
      }
      if (isnan(state.page_recorded_min[i]) || canon < state.page_recorded_min[i]) {
        state.page_recorded_min[i] = canon;
      }
    }
  }
}

void AppLoopTick() {
  const uint32_t now_ms = millis();
  static bool prev_pressed = false;
  static bool prev_demo = true;
  static bool auto_wifi_triggered = false;
  static uint32_t last_oled_probe_ms = 0;
  static uint8_t oled_watchdog_backoff_idx = 0;
  static size_t oled_retry_idx = 0;
  static bool oled_retry_done = false;
  static uint32_t last_oled_retry_ms = 0;
  static bool oled_missing_logged = false;
  static bool i2c_clock_raised = false;
  static uint32_t last_i2c_log_snapshot_ms = 0;

  WifiDiagTick(now_ms);

  if (!auto_wifi_triggered && kAutoWifiNoOledDelayMs > 0) {
    bool no_oled = false;
    bool wifi_active = false;
    uint32_t boot_ms = 0;
    portENTER_CRITICAL(&g_state_mux);
    no_oled = !g_state.oled_primary_ready && !g_state.oled_secondary_ready;
    wifi_active = g_state.wifi_mode_active;
    boot_ms = g_state.boot_ms;
    portEXIT_CRITICAL(&g_state_mux);
    if (no_oled && !wifi_active && (now_ms - boot_ms) >= kAutoWifiNoOledDelayMs) {
      bool enter_wifi = false;
      portENTER_CRITICAL(&g_state_mux);
      if (!g_state.wifi_mode_active) {
        g_state.wifi_mode_active = true;
        enter_wifi = true;
      }
      portEXIT_CRITICAL(&g_state_mux);
      if (enter_wifi) {
        LOGW("No OLED detected after %lu ms, entering Wi-Fi mode\r\n",
             static_cast<unsigned long>(now_ms - boot_ms));
        WifiPortalEnter();
      }
      auto_wifi_triggered = true;
    }
  }

  // Short boot-only OLED bring-up retries (non-blocking, 0..3s window).
  {
    uint32_t boot_ms = 0;
    portENTER_CRITICAL(&g_state_mux);
    boot_ms = g_state.boot_ms;
    portEXIT_CRITICAL(&g_state_mux);
    if (!oled_retry_done && boot_ms != 0) {
      const uint32_t elapsed = now_ms - boot_ms;
      const uint32_t kRetryOffsetsMs[] = {200, 500, 1000, 2000, 3000};
      const size_t kRetryCount = sizeof(kRetryOffsetsMs) / sizeof(kRetryOffsetsMs[0]);
      const bool safe_i2c = AppConfig::kSafeCableI2cEnabled;
      const uint32_t oled1_boot_hz = AppConfig::kBootI2cClockHz;
      const uint32_t oled2_boot_hz =
          safe_i2c ? AppConfig::kSafeCableBootI2cHz
                   : AppConfig::kI2c2FrequencyHz;
      const uint32_t oled1_runtime_hz =
          safe_i2c ? AppConfig::kSafeCableRuntimeI2cHz
                   : AppConfig::kI2cFrequencyHz;
      const uint32_t oled2_runtime_hz =
          safe_i2c ? AppConfig::kSafeCableRuntimeI2cHz
                   : AppConfig::kI2c2FrequencyHz;
      if (elapsed <= 5000U && !i2c_clock_raised) {
        Wire.setClock(oled1_boot_hz);
      }
      const bool retry_due =
          (oled_retry_idx < kRetryCount &&
           elapsed >= kRetryOffsetsMs[oled_retry_idx]) ||
          (oled_retry_idx >= kRetryCount &&
           (now_ms - last_oled_retry_ms) >= 1000U);
      if (retry_due) {
        last_oled_retry_ms = now_ms;
        bool primary_ready = false;
        bool secondary_ready = false;
        DisplayTopology topo = DisplayTopology::kSmallOnly;
        bool flip0 = false;
        bool flip1 = false;
        portENTER_CRITICAL(&g_state_mux);
        primary_ready = g_state.oled_primary_ready;
        secondary_ready = g_state.oled_secondary_ready;
        topo = g_state.display_topology;
        flip0 = g_state.screen_cfg[0].flip_180;
        flip1 = g_state.screen_cfg[1].flip_180;
        portEXIT_CRITICAL(&g_state_mux);

        if (kEnableVerboseSerialLogs) {
          LOGI("OLED late retry %u/%u (t=%lums)\r\n",
               static_cast<unsigned>(oled_retry_idx + 1),
               static_cast<unsigned>(kRetryCount),
               static_cast<unsigned long>(elapsed));
        }

        if (!primary_ready) {
          const bool recovered =
              RecoverI2cBus(Pins::kI2cSda, Pins::kI2cScl, "OLED1",
                            kEnableVerboseSerialLogs);
          bool ack = recovered && ProbeOled1Ack();
          if (!ack) {
            (void)RecoverI2cBus(Pins::kI2cSda, Pins::kI2cScl, "OLED1",
                                kEnableVerboseSerialLogs, true);
            ack = ProbeOled1Ack();
          }
          if (!ack && MaybeResetWireOled1(now_ms, oled1_boot_hz,
                                          kEnableVerboseSerialLogs)) {
            const bool ack_after_reset = ProbeOled1Ack();
            if (kEnableVerboseSerialLogs) {
              LOGI("OLED1 ACK after Wire reset %s\r\n",
                   ack_after_reset ? "ok" : "fail");
            }
            ack = ack_after_reset;
          }
          I2cOledLogEvent(1, I2cOledAction::kProbe, ack, Pins::kI2cSda,
                          Pins::kI2cScl);
          if (kEnableVerboseSerialLogs) {
            LOGI("OLED1 ACK %s\r\n", ack ? "ok" : "fail");
          }
          bool ok = false;
          if (ack) {
            if (topo == DisplayTopology::kLargeOnly ||
                topo == DisplayTopology::kLargePlusSmall) {
              ok = initAndTestDisplay64(g_oled_primary, true, oled1_boot_hz);
            } else {
              ok = initAndTestDisplay(g_oled_primary, true, oled1_boot_hz);
            }
          }
          if (ok) {
            g_oled_primary.setRotation(flip0);
            g_oled_primary.clearDisplay();
            const bool ack_after = ProbeOled1Ack();
            I2cOledLogEvent(1, I2cOledAction::kClear, ack_after, Pins::kI2cSda,
                            Pins::kI2cScl);
            if (kEnableVerboseSerialLogs) {
              LOGI("OLED1 re-ACK %s\r\n", ack_after ? "ok" : "fail");
            }
            ok = ack_after;
            if (ok) {
              g_oled_primary.sendRawCommand(0xAF);
            }
          }
          portENTER_CRITICAL(&g_state_mux);
          g_state.oled_primary_ready = ok;
          portEXIT_CRITICAL(&g_state_mux);
        }

        if (!secondary_ready) {
          const bool recovered =
              RecoverI2cBus(Pins::kI2c2Sda, Pins::kI2c2Scl, "OLED2",
                            kEnableVerboseSerialLogs);
          g_oled_secondary.setBusClockHz(oled2_boot_hz);
          bool ack = recovered && g_oled_secondary.probeAddress();
          if (!ack) {
            (void)RecoverI2cBus(Pins::kI2c2Sda, Pins::kI2c2Scl, "OLED2",
                                kEnableVerboseSerialLogs, true);
            ack = g_oled_secondary.probeAddress();
          }
          if (kEnableVerboseSerialLogs) {
            LOGI("OLED2 ACK %s\r\n", ack ? "ok" : "fail");
          }
          bool ok = false;
          if (ack) {
            ok = initAndTestDisplay(g_oled_secondary, false, oled2_boot_hz);
          }
          if (ok) {
            g_oled_secondary.setRotation(flip1);
            g_oled_secondary.clearDisplay();
            const bool ack_after = g_oled_secondary.probeAddress();
            I2cOledLogEvent(2, I2cOledAction::kClear, ack_after,
                            Pins::kI2c2Sda, Pins::kI2c2Scl);
            if (kEnableVerboseSerialLogs) {
              LOGI("OLED2 re-ACK %s\r\n", ack_after ? "ok" : "fail");
            }
            ok = ack_after;
            if (ok) {
              g_oled_secondary.sendRawCommand(0xAF);
            }
          }
          portENTER_CRITICAL(&g_state_mux);
          g_state.oled_secondary_ready = ok;
          portEXIT_CRITICAL(&g_state_mux);
        }

        if (oled_retry_idx < kRetryCount) {
          ++oled_retry_idx;
        }
        bool primary_ready_final = false;
        bool secondary_ready_final = false;
        portENTER_CRITICAL(&g_state_mux);
        primary_ready_final = g_state.oled_primary_ready;
        secondary_ready_final = g_state.oled_secondary_ready;
        portEXIT_CRITICAL(&g_state_mux);
        if (primary_ready_final && secondary_ready_final) {
          oled_retry_done = true;
        } else if (elapsed >= 3000U && !oled_missing_logged) {
          if (kEnableVerboseSerialLogs) {
            if (!primary_ready_final) {
              LOGW("OLED1 absent after late retries\r\n");
            }
            if (!secondary_ready_final) {
              LOGW("OLED2 absent after late retries\r\n");
            }
          }
          oled_missing_logged = true;
        }
      }
      if (!i2c_clock_raised) {
        bool primary_ready = false;
        bool secondary_ready = false;
        portENTER_CRITICAL(&g_state_mux);
        primary_ready = g_state.oled_primary_ready;
        secondary_ready = g_state.oled_secondary_ready;
        portEXIT_CRITICAL(&g_state_mux);
        if (primary_ready && secondary_ready &&
            ((oled1_runtime_hz > oled1_boot_hz) ||
             (oled2_runtime_hz != oled2_boot_hz))) {
          const bool ack1 = ProbeOled1Ack();
          const bool ack2 = g_oled_secondary.probeAddress();
          if (ack1 && ack2) {
            g_oled_primary.setBusClockHz(oled1_runtime_hz);
            g_oled_secondary.setBusClockHz(oled2_runtime_hz);
            i2c_clock_raised = true;
            LOGI("OLED1 bus clock = %lu Hz (runtime)\r\n",
                 static_cast<unsigned long>(oled1_runtime_hz));
            LOGI("OLED2 bus clock = %lu Hz (runtime)\r\n",
                 static_cast<unsigned long>(oled2_runtime_hz));
          }
        }
      }
    }
  }

  // Boot OLED watchdog: re-probe and retry init (0..60s) with backoff.
  {
    uint32_t boot_ms = 0;
    portENTER_CRITICAL(&g_state_mux);
    boot_ms = g_state.boot_ms;
    portEXIT_CRITICAL(&g_state_mux);
    if (boot_ms != 0 && (now_ms - boot_ms) <= 60000U) {
      const uint32_t watchdog_slow_hz = AppConfig::kSafeCableBootI2cHz;
      const uint32_t watchdog_fast_hz = AppConfig::kSafeCableRuntimeI2cHz;
      const uint32_t kBackoffMs[] = {1000U, 2000U, 5000U};
      const size_t kBackoffCount = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]);
      const uint32_t due_ms =
          (last_oled_probe_ms == 0) ? now_ms
                                    : (last_oled_probe_ms +
                                       kBackoffMs[oled_watchdog_backoff_idx]);
      if (now_ms >= due_ms) {
        last_oled_probe_ms = now_ms;
        bool primary_ready = false;
        bool secondary_ready = false;
        DisplayTopology topo = DisplayTopology::kSmallOnly;
        bool flip0 = false;
        bool flip1 = false;
        portENTER_CRITICAL(&g_state_mux);
        primary_ready = g_state.oled_primary_ready;
        secondary_ready = g_state.oled_secondary_ready;
        topo = g_state.display_topology;
        flip0 = g_state.screen_cfg[0].flip_180;
        flip1 = g_state.screen_cfg[1].flip_180;
        portEXIT_CRITICAL(&g_state_mux);

        bool any_attempt = false;
        bool any_success = false;

        const bool primary_ack = primary_ready ? ProbeOled1Ack() : false;
        if (!primary_ready || !primary_ack) {
          any_attempt = true;
          g_oled_primary.setBusClockHz(watchdog_slow_hz);
          RecoverI2cBus(Pins::kI2cSda, Pins::kI2cScl, "OLED1",
                        kEnableVerboseSerialLogs, true);
          bool ok = false;
          bool ack = ProbeOled1Ack();
          if (!ack && MaybeResetWireOled1(now_ms, watchdog_slow_hz,
                                          kEnableVerboseSerialLogs)) {
            const bool ack_after_reset = ProbeOled1Ack();
            if (kEnableVerboseSerialLogs) {
              LOGI("OLED1 ACK after Wire reset %s\r\n",
                   ack_after_reset ? "ok" : "fail");
            }
            ack = ack_after_reset;
          }
          I2cOledLogEvent(1, I2cOledAction::kProbe, ack, Pins::kI2cSda,
                          Pins::kI2cScl);
          if (kEnableVerboseSerialLogs) {
            LOGI("OLED1 ACK %s\r\n", ack ? "ok" : "fail");
          }
          if (ack) {
            if (topo == DisplayTopology::kLargeOnly ||
                topo == DisplayTopology::kLargePlusSmall) {
              ok = initAndTestDisplay64(g_oled_primary, true, watchdog_slow_hz);
            } else {
              ok = initAndTestDisplay(g_oled_primary, true, watchdog_slow_hz);
            }
          }
          if (ok) {
            g_oled_primary.setRotation(flip0);
            g_oled_primary.clearDisplay();
            const bool ack_after = ProbeOled1Ack();
            I2cOledLogEvent(1, I2cOledAction::kClear, ack_after, Pins::kI2cSda,
                            Pins::kI2cScl);
            if (kEnableVerboseSerialLogs) {
              LOGI("OLED1 watchdog re-ACK %s\r\n", ack_after ? "ok" : "fail");
            }
            ok = ack_after;
            if (ok) {
              g_oled_primary.sendRawCommand(0xAF);
              g_oled_primary.setBusClockHz(watchdog_fast_hz);
            }
          }
          if (ok) {
            any_success = true;
          }
          portENTER_CRITICAL(&g_state_mux);
          g_state.oled_primary_ready = ok;
          portEXIT_CRITICAL(&g_state_mux);
        }

        const bool secondary_ack =
            secondary_ready ? g_oled_secondary.probeAddress() : false;
        if (!secondary_ready || !secondary_ack) {
          any_attempt = true;
          g_oled_secondary.setBusClockHz(watchdog_slow_hz);
          RecoverI2cBus(Pins::kI2c2Sda, Pins::kI2c2Scl, "OLED2",
                        kEnableVerboseSerialLogs, true);
          bool ok = false;
          bool ack = g_oled_secondary.probeAddress();
          if (kEnableVerboseSerialLogs) {
            LOGI("OLED2 ACK %s\r\n", ack ? "ok" : "fail");
          }
          if (ack) {
            ok =
                initAndTestDisplay(g_oled_secondary, false, watchdog_slow_hz);
          }
          if (ok) {
            g_oled_secondary.setRotation(flip1);
            g_oled_secondary.clearDisplay();
            const bool ack_after = g_oled_secondary.probeAddress();
            I2cOledLogEvent(2, I2cOledAction::kClear, ack_after,
                            Pins::kI2c2Sda, Pins::kI2c2Scl);
            if (kEnableVerboseSerialLogs) {
              LOGI("OLED2 watchdog re-ACK %s\r\n", ack_after ? "ok" : "fail");
            }
            ok = ack_after;
            if (ok) {
              g_oled_secondary.sendRawCommand(0xAF);
              g_oled_secondary.setBusClockHz(watchdog_fast_hz);
            }
          }
          if (ok) {
            any_success = true;
          }
          portENTER_CRITICAL(&g_state_mux);
          g_state.oled_secondary_ready = ok;
          portEXIT_CRITICAL(&g_state_mux);
        }

        if (any_attempt) {
          if (any_success) {
            oled_watchdog_backoff_idx = 0;
          } else if (oled_watchdog_backoff_idx + 1 < kBackoffCount) {
            ++oled_watchdog_backoff_idx;
          }
        }

        if ((now_ms - boot_ms) >= 5000U) {
          bool primary_final = false;
          bool secondary_final = false;
          portENTER_CRITICAL(&g_state_mux);
          primary_final = g_state.oled_primary_ready;
          secondary_final = g_state.oled_secondary_ready;
          portEXIT_CRITICAL(&g_state_mux);
          const bool missing = (!primary_final || !secondary_final);
          if (missing &&
              (last_i2c_log_snapshot_ms == 0 ||
               (now_ms - last_i2c_log_snapshot_ms) >= 60000U)) {
            const bool saved = I2cOledLogSaveSnapshot(now_ms);
            last_i2c_log_snapshot_ms = now_ms;
            if (kEnableVerboseSerialLogs) {
              LOGW("I2C/OLED log snapshot %s\r\n",
                   saved ? "saved" : "failed");
            }
            if (!kEnableVerboseSerialLogs) {
              (void)saved;
            }
          }
        }
      }
    }
  }

  // Repeat I2C scan a few times so it's visible even if monitor opens late (optional).
  if (kEnableBootI2cScan) {
    if (!g_i2c_seen && g_i2c_scan_attempts < 4 &&
        (now_ms - g_last_i2c_scan_ms) >= 5000U) {
      g_i2c_seen = ScanI2cBuses();
      Wire.begin(Pins::kI2cSda, Pins::kI2cScl);
      g_wire_sda_pin = Pins::kI2cSda;
      g_wire_scl_pin = Pins::kI2cScl;
      {
        uint32_t bus_hz = g_oled_primary.busClockHz();
        if (bus_hz == 0) {
          bus_hz = AppConfig::kBootI2cClockHz;
        }
        Wire.setClock(bus_hz);
      }
      Wire.setTimeOut(20);
      ++g_i2c_scan_attempts;
      g_last_i2c_scan_ms = now_ms;
    }
  }

  // Lightweight serial heartbeat to confirm firmware is running (gated for prod).
  if (kEnableHeartbeatSerial && kEnablePeriodicDiagLogs) {
    static uint32_t last_hb_ms = 0;
    const uint32_t kHeartbeatPeriodMs = 2000;
    if ((now_ms - last_hb_ms) >= kHeartbeatPeriodMs) {
      uint32_t last_can_rx_ms = 0;
      portENTER_CRITICAL(&g_state_mux);
      last_can_rx_ms = g_state.last_can_rx_ms;
      portEXIT_CRITICAL(&g_state_mux);
      const bool rx_recent =
          (last_can_rx_ms != 0) && ((now_ms - last_can_rx_ms) < 1000U);
      LOGI(
          "[HB] up=%lus wifi=%s demo=%s can=%s rx=%s sleep=%s\r\n",
          static_cast<unsigned long>(now_ms / 1000UL),
          g_state.wifi_mode_active ? "on" : "off",
          g_state.demo_mode ? "on" : "off",
          g_state.can_ready ? "ready" : "down",
          rx_recent ? "yes" : "no",
          g_state.sleep ? "yes" : "no");
      (void)rx_recent;
      last_hb_ms = now_ms;
    }
  }
  if (g_state.can_ready && (!g_twai.isStarted() || !g_twai.isNormalMode())) {
    portENTER_CRITICAL(&g_state_mux);
    g_state.can_ready = false;
    portEXIT_CRITICAL(&g_state_mux);
    if (!g_twai.isNormalMode()) {
      LOGW("CAN started but not NORMAL (NO ACK)\r\n");
    }
  }

  WifiPortalTick();
  InputRuntimeTick(now_ms);
  if (g_state.wifi_mode_active && wifi_ota_in_progress) {
    AppSleepMs(1);
    return;
  }
  if (kEnablePeriodicDiagLogs) {
    const uint32_t wifi_slice_dt = millis() - now_ms;
    static uint32_t last_wifi_warn_ms = 0;
    if (wifi_slice_dt > 50 && (now_ms - last_wifi_warn_ms) > 5000U) {
      LOGW("[WIFI_DIAG] WARN: wifi slice=%lums\n",
           static_cast<unsigned long>(wifi_slice_dt));
      last_wifi_warn_ms = now_ms;
    }
    static uint32_t last_dns_count = 0;
    static uint32_t last_dns_check_ms = 0;
    if ((now_ms - last_dns_check_ms) >= 5000U) {
      const uint32_t curr_dns = WifiPortalDnsCount();
      const uint32_t dns_max = WifiPortalDnsMaxUs();
      const uint16_t sta = WiFi.softAPgetStationNum();
      if (sta > 0) {
        LOGI("[DNS] sta=%u req=%lu max_us=%lu\n",
             static_cast<unsigned>(sta),
             static_cast<unsigned long>(curr_dns),
             static_cast<unsigned long>(dns_max));
      }
      (void)dns_max;
      if (sta > 0 && curr_dns == last_dns_count) {
        LOGW("[WIFI_DIAG] WATCHDOG: no DNS/HTTP in 5s, heap=%u min=%u maxblk=%u mode=%d status=%d\n",
             ESP.getFreeHeap(), ESP.getMinFreeHeap(),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             WiFi.getMode(), static_cast<int>(WiFi.status()));
      }
      last_dns_count = curr_dns;
      last_dns_check_ms = now_ms;
    }
  }
  if (g_state.baro_reset_request) {
    g_state.baro_reset_request = false;
    ResetBaroPersist();
    if (g_state.oled_primary_ready) {
      g_oled_primary.drawLines("BARO RESET", "Rebooting...", nullptr, nullptr);
    }
    if (g_state.oled_secondary_ready) {
      g_oled_secondary.drawLines("BARO RESET", "Rebooting...", nullptr, nullptr);
    }
    AppSleepMs(150);
    ESP.restart();
    return;
  }
  const bool had_can_recently =
      (g_state.last_can_rx_ms != 0) && ((now_ms - g_state.last_can_rx_ms) < 500U);
  if (g_state.wifi_mode_active) {
    static uint32_t last_wifi_render_ms = 0;
    const uint32_t kWifiRenderIntervalMs = 500;
    const bool want_can = AppConfig::kCanRuntimeSupported &&
                          AppConfig::IsRealCanEnabled() && !g_state.demo_mode;
    if (want_can) {
      MaybeTriggerUnlockedRescan(g_state, now_ms);
      EnsureCanStartedOrScan(g_state);
      CanRuntimeTick(now_ms);
    } else if (!had_can_recently && g_state.demo_mode) {
      DemoTick(g_state, now_ms);
    }
    const bool allow_oled_wifi =
        (now_ms - last_wifi_render_ms) >= kWifiRenderIntervalMs;
    if (allow_oled_wifi) {
      last_wifi_render_ms = now_ms;
      AutoAcquireBaro(g_state, now_ms);
      renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary, now_ms,
               true, true, g_alerts);
    }
    PersistRuntimeTick(now_ms);
    AppSleepMs(1);
    return;
  }
  if (g_state.sleep) {
    AppSleepMs(1);
    return;
  }

#if SETUP_WIZARD_ENABLED
  if (g_auto_setup_pending && !g_setup_wizard.isActive() &&
      !g_state.ui_menu.isActive() && AppConfig::IsRealCanEnabled() &&
      !g_state.demo_mode) {
    g_setup_wizard.begin(g_state.focus_screen, g_state);
    g_auto_setup_pending = false;
    AppSleepMs(1);
    return;
  }

  if (g_setup_wizard.isActive()) {
    g_setup_wizard.tick(g_state, now_ms);
    g_setup_wizard.render(g_state, g_oled_primary, g_oled_secondary, now_ms);
    g_state.wizard_active = g_setup_wizard.isActive();
    AppSleepMs(1);
    return;
  }
#endif

  g_alerts.update(g_state, ActiveStore(), now_ms);
  const bool want_can = AppConfig::kCanRuntimeSupported &&
                        AppConfig::IsRealCanEnabled() && !g_state.demo_mode;
  if (prev_demo && want_can) {
    g_datastore_can = DataStore();  // clear demo values; show stale until CAN updates
    for (uint8_t z = 0; z < kMaxZones; ++z) {
      g_state.force_redraw[z] = true;
    }
  }
  if (want_can) {
    MaybeTriggerUnlockedRescan(g_state, now_ms);
    EnsureCanStartedOrScan(g_state);
    CanRuntimeTick(now_ms);
  }

  if (!want_can && g_state.demo_mode && !had_can_recently) {
    DemoTick(g_state, now_ms);
  }

  AutoAcquireBaro(g_state, now_ms);

  if (g_state.self_test_enabled) {
    if ((now_ms - g_state.self_test.last_step_ms) >=
        AppConfig::kUiSelfTestPeriodMs) {
      g_state.self_test.last_step_ms = now_ms;
      g_state.self_test.step =
          static_cast<uint8_t>((g_state.self_test.step + 1) % 7);
      applySelfTestStep(g_state, g_state.self_test.step);
    }
  }

  if (g_state.self_test.request_reset_all) {
    resetAllMax(g_state, now_ms);
    g_state.self_test.request_reset_all = false;
  }

  updateRecordedExtrema(g_state, ActiveStore(), now_ms);

  // Edit timeout
  for (uint8_t scr = 0; scr < kMaxZones; ++scr) {
    if (g_state.edit_mode.mode[scr] != EditModeState::Mode::kNone) {
      if ((now_ms - g_state.edit_mode.last_activity_ms[scr]) > 15000) {
        exitEditMode(g_state, scr);
      }
    }
  }

  bool btn_pressed = false;
  portENTER_CRITICAL(&g_state_mux);
  btn_pressed = g_state.btn_pressed;
  portEXIT_CRITICAL(&g_state_mux);
  if (prev_pressed && !btn_pressed) {
    g_state.allow_oled2_during_hold = false;
  }
  prev_pressed = btn_pressed;
  prev_demo = g_state.demo_mode;

#ifdef DEBUG_STALE_OLED2
  {
    static uint32_t last_map_dbg_ms = 0;
    if ((now_ms - last_map_dbg_ms) >= 1000U) {
#if CORE_DEBUG_LEVEL >= 3
      if (kEnableVerboseSerialLogs) {
        const SignalRead r = ActiveStore().get(SignalId::kMap, now_ms);
        LOGI(
            "[MAPDBG] t=%lu age=%lu flags=0x%02X valid=%d val=%.3f last_can=%lu\n",
            static_cast<unsigned long>(now_ms),
            static_cast<unsigned long>(r.age_ms), r.flags, r.valid,
            static_cast<double>(r.value),
            static_cast<unsigned long>(g_state.last_can_rx_ms));
      }
#endif
      last_map_dbg_ms = now_ms;
    }
  }
#endif

  uint8_t secondary_zone_id = 1;
  if (g_state.display_topology == DisplayTopology::kDualSmall ||
      g_state.display_topology == DisplayTopology::kLargePlusSmall) {
    secondary_zone_id = 2;
  }
  const bool editing_oled2 =
      g_state.edit_mode.mode[secondary_zone_id] != EditModeState::Mode::kNone &&
      g_state.edit_mode.page[secondary_zone_id] ==
          currentPageIndex(g_state, secondary_zone_id);
  uint32_t oled1_bus_hz = g_oled_primary.busClockHz();
  if (oled1_bus_hz == 0) {
    oled1_bus_hz = AppConfig::kI2cFrequencyHz;
  }
  uint32_t oled1_interval = (oled1_bus_hz <= 25000U) ? 250U : 125U;
  bool allow_oled1 =
      (now_ms - g_state.last_oled_ms[0] >= oled1_interval) ||
      g_state.force_redraw[0];

  uint32_t oled2_bus_hz = g_oled_secondary.busClockHz();
  if (oled2_bus_hz == 0) {
    oled2_bus_hz = AppConfig::kI2c2FrequencyHz;
  }
  uint32_t oled2_interval = 250U;
  if (oled2_bus_hz <= 25000U) {
    // Slow bus: reduce update rate under load to avoid jitter.
    oled2_interval = (want_can ? 400U : 250U);
  } else if (oled2_bus_hz <= 50000U) {
    oled2_interval = 250U;
  } else {
    oled2_interval = 250U;
  }
  if (editing_oled2 && oled2_interval < 250U) {
    oled2_interval = 250U;
  }
  static uint8_t prev_focus = 0;
  if (g_state.focus_screen != prev_focus) {
    g_state.force_redraw[0] = true;
    g_state.force_redraw[secondary_zone_id] = true;
    g_state.force_redraw[2] = true;
    prev_focus = g_state.focus_screen;
  }
  bool allow_oled2 =
      ((now_ms - g_state.last_oled_ms[secondary_zone_id] >= oled2_interval) ||
       g_state.force_redraw[secondary_zone_id]) &&
      ((now_ms - g_state.last_oled_ms[0]) >= 50U);

  // Stagger at very slow I2C: never send both buffers in the same tick.
  static bool prefer_primary_next = true;
  const bool slow_bus =
      (g_oled_primary.busClockHz() != 0 &&
       g_oled_primary.busClockHz() <= 25000U) ||
      (g_oled_secondary.busClockHz() != 0 &&
       g_oled_secondary.busClockHz() <= 25000U);
  if (slow_bus && allow_oled1 && allow_oled2) {
    if (prefer_primary_next) {
      allow_oled2 = false;
    } else {
      // Defer OLED1 this tick.
      if (!g_state.force_redraw[0]) {
        allow_oled1 = false;
      } else {
        allow_oled2 = false;
      }
    }
    prefer_primary_next = !prefer_primary_next;
  }

  // FPS instrumentation
  static uint32_t fps1_count = 0;
  static uint32_t fps2_count = 0;
  static uint32_t last_fps_print_ms = 0;
  const bool rendered1 =
      (allow_oled1 && g_state.oled_primary_ready);  // candidate for counting
  const bool rendered2 =
      (allow_oled2 && g_state.oled_secondary_ready);  // candidate for counting

  renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary, now_ms,
           allow_oled1, allow_oled2, g_alerts);

  if (rendered1) ++fps1_count;
  if (rendered2) ++fps2_count;
  if ((now_ms - last_fps_print_ms) >= 1000U) {
    LOGI("OLED1 fps=%lu OLED2 fps=%lu\r\n",
         static_cast<unsigned long>(fps1_count),
         static_cast<unsigned long>(fps2_count));
    fps1_count = 0;
    fps2_count = 0;
    last_fps_print_ms = now_ms;
  }
  PersistRuntimeTick(now_ms);
  AppSleepMs(1);
}
