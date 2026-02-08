#include "app/can_bootstrap.h"

// THREADING CONTRACT (g_state):
// - Writers: CAN bootstrap runs on main loop; CAN RX task writes CAN stats/state.
// - Any shared g_state writes here (can_ready/bitrate/masks) must be under g_state_mux.
// - Readers should prefer snapshots for cross-task access.

#include <Arduino.h>

#include "app/app_globals.h"
#include "app_config.h"
#include "app/can_recovery_eval.h"
#include "config/logging.h"
#include "pins.h"

void EnsureCanStartedOrScan(AppState& s) {
  // Non-blocking state for CAN recover/scan to avoid long stalls in AppLoop.
  struct {
    bool active = false;
    bool recover_mode = false;
    uint32_t start_ms = 0;
    uint32_t window_ms = 0;
    uint16_t min_dash = 0;
    uint32_t rx_dash = 0;
    uint32_t bus_off = 0;
    uint32_t err_passive = 0;
    uint32_t rx_overrun = 0;
    uint32_t rx_missed = 0;
    uint32_t bitrate = 0;
    bool started_ok = false;
    uint32_t last_wait_log_ms = 0;
  } static scan_ctx;
  const uint32_t slice_budget_ms = 3;  // max work per call

  if (s.demo_mode || !AppConfig::IsRealCanEnabled()) {
    return;
  }
  if (s.can_safe_listen) {
    return;
  }

  // If a scan is in progress, advance it with a small time budget.
  if (scan_ctx.active) {
    const uint32_t now_ms = millis();
    const uint32_t slice_start = now_ms;
    while ((millis() - slice_start) < slice_budget_ms) {
      twai_message_t msg{};
      if (g_twai.receive(msg, 0)) {
        const IEcuProfile& profile = g_ecu_mgr.profile();
        const int idx = profile.dashIndexForId(msg.identifier);
        if (idx >= 0) {
          ++scan_ctx.rx_dash;
          if (idx < 8) {
            const uint8_t bit = static_cast<uint8_t>(1U << idx);
            portENTER_CRITICAL(&g_state_mux);
            s.id_present_mask |= bit;
            portEXIT_CRITICAL(&g_state_mux);
          }
        }
      } else {
        uint32_t alerts = 0;
        while (g_twai.readAlerts(alerts, 0)) {
          if (alerts & TWAI_ALERT_BUS_OFF) ++scan_ctx.bus_off;
          if (alerts & TWAI_ALERT_ERR_PASS) ++scan_ctx.err_passive;
          if (alerts & TWAI_ALERT_RX_QUEUE_FULL) ++scan_ctx.rx_overrun;
        }
        break;  // no frame now, exit slice
      }
    }
    const bool window_done = (millis() - scan_ctx.start_ms) >= scan_ctx.window_ms;
    const bool hit_goal = scan_ctx.rx_dash >= scan_ctx.min_dash;
    const bool has_error = scan_ctx.bus_off || scan_ctx.err_passive || scan_ctx.rx_overrun;
    if (window_done || hit_goal || has_error) {
      twai_status_info_t st{};
      if (twai_get_status_info(&st) == ESP_OK) {
        scan_ctx.rx_missed = st.rx_missed_count;
      }
      const uint16_t max_missed = 2;
      CanScanEvalIn eval_in{};
      eval_in.started_ok = scan_ctx.started_ok;
      eval_in.normal_mode = g_twai.isNormalMode();
      eval_in.rx_dash = scan_ctx.rx_dash;
      eval_in.min_dash = scan_ctx.min_dash;
      eval_in.bus_off = scan_ctx.bus_off;
      eval_in.err_passive = scan_ctx.err_passive;
      eval_in.rx_overrun = scan_ctx.rx_overrun;
      eval_in.rx_missed = scan_ctx.rx_missed;
      const bool ok = CanScanEvalOk(eval_in, max_missed);
      if (ok) {
        uint8_t id_mask = 0;
        portENTER_CRITICAL(&g_state_mux);
        s.can_ready = true;
        s.can_need_recover = false;
        s.can_bitrate_locked = true;
        s.can_recover_backoff_ms = 5000;
        s.can_recover_last_attempt_ms = scan_ctx.start_ms;
        s.can_bitrate_value = scan_ctx.bitrate;
        id_mask = s.id_present_mask;
        portEXIT_CRITICAL(&g_state_mux);
        CanSettings locked{};
        locked.bitrate_locked = true;
        locked.bitrate_value = scan_ctx.bitrate;
        locked.id_present_mask = id_mask;
        locked.hash_match = true;
        g_nvs.saveCanSettings(locked);
        LOGI("CAN started @%lu normal\r\n",
             static_cast<unsigned long>(scan_ctx.bitrate));
      } else if (has_error) {
        g_twai.stop();
        g_twai.uninstall();
        pinMode(Pins::kCanTx, INPUT_PULLUP);
        portENTER_CRITICAL(&g_state_mux);
        s.can_ready = false;
        s.can_recover_last_attempt_ms = scan_ctx.start_ms;
        s.can_recover_backoff_ms =
            (s.can_recover_backoff_ms < 20000U) ? (s.can_recover_backoff_ms * 2)
                                                : s.can_recover_backoff_ms;
        portEXIT_CRITICAL(&g_state_mux);
      } else {
        // No bus errors: keep TWAI running and mark CAN up but unlocked.
        const bool twai_up = g_twai.isStarted() && g_twai.isNormalMode();
        if (twai_up) {
          // Explicit guard: never tear down TWAI on non-error failures.
        }
        portENTER_CRITICAL(&g_state_mux);
        s.can_ready = true;
        s.can_need_recover = false;
        portEXIT_CRITICAL(&g_state_mux);
        LOGI("CAN up @%lu (unlocked, waiting frames)\r\n",
             static_cast<unsigned long>(scan_ctx.bitrate));
      }
      scan_ctx = {};
      return;
    }
    return;  // defer remaining work to next tick
  }

  if (s.can_need_recover) {
    const uint32_t now_ms = millis();
    const uint32_t since_attempt = now_ms - s.can_recover_last_attempt_ms;
    if (since_attempt >= s.can_recover_backoff_ms &&
        (now_ms - s.last_bus_off_ms) >= 5000U) {
      const bool started = g_twai.startNormal(s.can_bitrate_value);
      if (!started) {
        portENTER_CRITICAL(&g_state_mux);
        s.can_recover_last_attempt_ms = now_ms;
        s.can_recover_backoff_ms =
            (s.can_recover_backoff_ms < 20000U) ? (s.can_recover_backoff_ms * 2)
                                                : s.can_recover_backoff_ms;
        portEXIT_CRITICAL(&g_state_mux);
        return;
      }
      scan_ctx.active = true;
      scan_ctx.recover_mode = true;
      scan_ctx.start_ms = now_ms;
      scan_ctx.window_ms = 400;
      scan_ctx.min_dash = 5;
      scan_ctx.rx_dash = 0;
      scan_ctx.bus_off = 0;
      scan_ctx.err_passive = 0;
      scan_ctx.rx_overrun = 0;
      scan_ctx.rx_missed = 0;
      scan_ctx.bitrate = s.can_bitrate_value;
      scan_ctx.started_ok = true;
      return;
    } else {
      return;
    }
  }
  if (s.can_ready) {
    return;
  }
  const uint32_t now_ms = millis();
  if (s.last_bus_off_ms != 0 && (now_ms - s.last_bus_off_ms) < 5000U) {
    return;  // rate-limit restart attempts after bus-off
  }
  const bool already_started = g_twai.isStarted();
  if (s.can_bitrate_locked) {
    if (!g_twai.startNormal(s.can_bitrate_value)) {
      pinMode(Pins::kCanTx, INPUT_PULLUP);
      portENTER_CRITICAL(&g_state_mux);
      s.can_ready = false;
      portEXIT_CRITICAL(&g_state_mux);
      return;
    }
    bool ready = false;
    portENTER_CRITICAL(&g_state_mux);
    s.can_ready = g_twai.isNormalMode();
    ready = s.can_ready;
    portEXIT_CRITICAL(&g_state_mux);
    if (!ready) {
      LOGE("CAN READY but TWAI stopped = BUG\r\n");
    } else {
      LOGI("CAN start locked @%lu\r\n",
           static_cast<unsigned long>(s.can_bitrate_value));
    }
    return;
  }
  const IEcuProfile& profile = g_ecu_mgr.profile();
  uint32_t rate = s.can_bitrate_value;
  if (rate == 0) {
    rate = profile.preferredBitrate();
    portENTER_CRITICAL(&g_state_mux);
    s.can_bitrate_value = rate;
    portEXIT_CRITICAL(&g_state_mux);
  }
  const ValidationSpec& vs = profile.validationSpec();
  const uint32_t window_ms = (vs.window_ms > 0) ? vs.window_ms : 300;
  const uint16_t min_dash = (vs.min_rx_dash > 0) ? vs.min_rx_dash : 12;
  bool started = already_started;
  if (!started) {
    g_twai.stop();
    g_twai.uninstall();
    started = g_twai.startNormal(rate);
  }
  if (started) {
    scan_ctx.active = true;
    scan_ctx.recover_mode = false;
    scan_ctx.start_ms = millis();
    scan_ctx.window_ms = window_ms;
    scan_ctx.min_dash = min_dash;
    scan_ctx.rx_dash = 0;
    scan_ctx.bus_off = 0;
    scan_ctx.err_passive = 0;
    scan_ctx.rx_overrun = 0;
    scan_ctx.rx_missed = 0;
    scan_ctx.bitrate = rate;
    scan_ctx.started_ok = true;
    return;
  }

  if (!started) {
    bool ready = false;
    portENTER_CRITICAL(&g_state_mux);
    s.can_ready = g_twai.isNormalMode();
    ready = s.can_ready;
    portEXIT_CRITICAL(&g_state_mux);
    if (!ready && started) {
      LOGE("CAN READY but TWAI stopped = BUG\r\n");
    }
#if SETUP_WIZARD_ENABLED
    if (!g_setup_wizard.isActive() && !s.ui_menu.isActive()) {
      g_setup_wizard.begin(s.focus_screen, s);
    }
#endif
  }
}
