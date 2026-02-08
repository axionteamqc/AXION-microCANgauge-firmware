#include "app/can_runtime.h"

// THREADING CONTRACT (g_state):
// - Writer: CAN RX task updates CAN stats/state; all shared g_state writes must be under g_state_mux.
// - Readers: UI/portal must use snapshots (CanStateSnapshot/AppUiSnapshot) or read under lock.
// - Single-writer fields (main loop only) may remain lock-free if not read cross-task.

#include <Arduino.h>

// Concurrency discipline:
// - Every write to g_state.* from the CAN task must be under g_state_mux.
// - Readers should prefer CanStateSnapshot/AppUiSnapshot to avoid torn reads.

#include "app/app_globals.h"
#include "app_config.h"
#include "config/logging.h"
#include "ecu/ecu_profile.h"
#include "pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <cmath>

namespace {

bool InRange(SignalId id, float phys) {
  switch (id) {
    case SignalId::kBatt:
      return phys >= 6.0f && phys <= 18.5f;
    case SignalId::kRpm:
      return phys >= 0.0f && phys <= 12000.0f;
    case SignalId::kMap:
      return phys >= 0.0f && phys <= 400.0f;  // kPa abs
    case SignalId::kTps:
      return phys >= 0.0f && phys <= 100.0f;
    case SignalId::kClt:
    case SignalId::kMat:
      return phys >= -40.0f && phys <= 300.0f;  // degF
    case SignalId::kAdv:
    case SignalId::kLaunchTiming:
    case SignalId::kTcRetard:
      return phys >= -40.0f && phys <= 80.0f;
    case SignalId::kAfr1:
    case SignalId::kAfrTarget1:
      return phys >= 5.0f && phys <= 25.0f;
    case SignalId::kVss1:
      return phys >= 0.0f && phys <= 120.0f;  // m/s ~ 432 km/h
    case SignalId::kPw1:
    case SignalId::kPw2:
    case SignalId::kPwSeq1:
      return phys >= 0.0f && phys <= 50.0f;  // ms
    case SignalId::kEgoCor1:
      return phys >= -50.0f && phys <= 200.0f;  // %
    case SignalId::kEgt1:
      return phys >= 0.0f && phys <= 2000.0f;  // degF (table uses degF)
    case SignalId::kKnkRetard:
      return phys >= 0.0f && phys <= 20.0f;
    case SignalId::kSensors1:
    case SignalId::kSensors2:
      return true;  // user-defined sensors: do not gate here
    default:
      return true;
  }
}

}  // namespace

constexpr uint32_t kCanLinkWindowMs = 1500;
constexpr uint32_t kCanMinFramesProfile = 5;
constexpr float kCanOobRatioThreshold = 0.80f;
constexpr float kCanOobRatioImplausible = 0.40f;
constexpr uint32_t kCanSafeListenMs = 4000;
constexpr uint32_t kCanLagMs = 1200;
constexpr uint32_t kCanNoFramesMs = 2000;
constexpr uint32_t kCanNoMatchMs = 1500;
constexpr uint32_t kCanBadDecodeMs = 1500;
constexpr uint32_t kCanImplausibleWindowMs = 2000;
constexpr uint32_t kCanImplausibleEvents = 20;
constexpr uint32_t kCanMinMatchSamples = 25;
static TaskHandle_t g_can_rx_task = nullptr;
static bool g_can_rx_task_started = false;
portMUX_TYPE g_state_mux = portMUX_INITIALIZER_UNLOCKED;

// Atomic-ish read of a counter that may be updated from ISR/task context.
static inline uint32_t ReadCanRxEdgeCount() {
#if defined(__GNUC__) || defined(__clang__)
  return __atomic_load_n(&g_can_rx_edge_count, __ATOMIC_RELAXED);
#else
  return g_can_rx_edge_count;
#endif
}

inline uint32_t ElapsedMs(uint32_t now, uint32_t then) {
  if (now >= then) return now - then;
  const uint32_t skew = then - now;
  if (skew <= 2000U) return 0;
  return now - then;  // wrap-around case
}

void UpdateCanHealth(AppState& s, uint32_t now_ms) {
  AppState::CanStats stats_snapshot{};
  uint32_t last_can_rx_ms = 0;
  uint32_t last_can_match_ms = 0;
  portENTER_CRITICAL(&g_state_mux);
  stats_snapshot = s.can_stats;
  last_can_rx_ms = s.last_can_rx_ms;
  last_can_match_ms = s.last_can_match_ms;
  portEXIT_CRITICAL(&g_state_mux);

  CanLinkDiag& d = s.can_link;
  auto reset_window = [&]() {
    d.window_start_ms = now_ms;
    d.rx_total_base = stats_snapshot.rx_total;
    d.rx_match_base = stats_snapshot.rx_match;
    d.rx_dash_base = stats_snapshot.rx_dash;
    d.decode_oob_base = stats_snapshot.decode_oob;
  };

  if (d.window_start_ms == 0 || (now_ms - d.window_start_ms) > kCanImplausibleWindowMs) {
    reset_window();
  }

  // Link-state inference (kept for compatibility)
  const bool no_frames = (last_can_rx_ms == 0) ||
                         (ElapsedMs(now_ms, last_can_rx_ms) > kCanNoFramesMs);
  CanLinkState desired = d.state;
  if (no_frames) {
    desired = CanLinkState::kNoFrames;
  } else if ((now_ms - d.window_start_ms) >= kCanLinkWindowMs) {
    const uint32_t total_delta = stats_snapshot.rx_total - d.rx_total_base;
    const uint32_t dash_delta = stats_snapshot.rx_dash - d.rx_dash_base;
    if (total_delta == 0 && dash_delta == 0) {
      desired = CanLinkState::kNoFrames;
    } else if (total_delta >= kCanMinFramesProfile && dash_delta == 0) {
      desired = CanLinkState::kNoProfileMatch;
    } else {
      desired = CanLinkState::kOk;
    }
    reset_window();
  }

  if (desired != d.state) {
    d.state = desired;
    d.last_change_ms = now_ms;
    for (uint8_t z = 0; z < kMaxZones; ++z) {
      s.force_redraw[z] = true;
    }
  }

  // Health evaluation: timers + implausible event counts
  CanHealth health = CanHealth::kOk;
  const bool seen_any = last_can_rx_ms != 0;
  const bool seen_match = last_can_match_ms != 0;
  const uint32_t since_any =
      seen_any ? ElapsedMs(now_ms, last_can_rx_ms) : 0xFFFFFFFFu;
  const uint32_t since_match =
      seen_match ? ElapsedMs(now_ms, last_can_match_ms) : 0xFFFFFFFFu;
  portENTER_CRITICAL(&g_state_mux);
  s.can_stats.stale_ms = since_any;
  portEXIT_CRITICAL(&g_state_mux);
  const uint32_t rx_delta = stats_snapshot.rx_total - d.rx_total_base;

  const bool edge_active = s.can_edge_active && (s.can_edge_rate > 200.0f);
  if (!seen_any || since_any > kCanNoFramesMs) {
    health = CanHealth::kNoFrames;
  } else if (since_any > kCanLagMs) {
    health = CanHealth::kStale;
  } else if (since_match > kCanNoMatchMs || edge_active) {
    health = CanHealth::kDecodeBad;  // wrong ECU/bitrate/config
  } else {
    const uint32_t match_delta = stats_snapshot.rx_match - d.rx_match_base;
    const uint32_t oob_delta = stats_snapshot.decode_oob - d.decode_oob_base;
    if (oob_delta >= kCanImplausibleEvents) {
      health = CanHealth::kImplausible;
    } else if (match_delta >= kCanMinMatchSamples) {
      const float ratio = static_cast<float>(oob_delta) /
                          static_cast<float>(match_delta);
      if (ratio > kCanOobRatioThreshold) {
        health = CanHealth::kDecodeBad;
      }
    }
  }

  if (d.health == CanHealth::kNoFrames) {
    const bool held_min_time =
        (now_ms - d.last_health_change_ms) < 1000U;
    const bool exit_ready = (seen_any && since_any <= 300U) || (rx_delta >= 2U);
    if (held_min_time || !exit_ready) {
      health = CanHealth::kNoFrames;
    }
  }

  if (health != d.health) {
    d.health = health;
    d.last_health_change_ms = now_ms;
    for (uint8_t z = 0; z < kMaxZones; ++z) {
      s.force_redraw[z] = true;
    }
  }

  const bool can_fault = (health == CanHealth::kDecodeBad) ||
                         (health == CanHealth::kImplausible);
  (void)can_fault;  // no automatic listen-only fallback
}

uint32_t CanRxTaskWatermark() {
  if (!g_can_rx_task) return 0;
  return uxTaskGetStackHighWaterMark(g_can_rx_task);
}

void UpdateCanEdges(AppState& s, uint32_t now_ms) {
  const uint32_t edges = ReadCanRxEdgeCount();
  if (s.can_edge_last_sample_ms == 0) {
    s.can_edge_last_sample_ms = now_ms;
    s.can_edge_prev = edges;
    return;
  }
  const uint32_t elapsed = now_ms - s.can_edge_last_sample_ms;
  if (elapsed < 250U) return;
  const uint32_t delta = (edges >= s.can_edge_prev) ? (edges - s.can_edge_prev) : 0;
  s.can_edge_rate = static_cast<float>(delta) * 1000.0f / static_cast<float>(elapsed);
  s.can_edge_active = (s.can_edge_rate > 200.0f);
  s.can_edge_prev = edges;
  s.can_edge_last_sample_ms = now_ms;
}

void UpdateCanRates(AppState& s, uint32_t now_ms) {
  AppState::CanRateWindow& w = s.can_rates;
  uint32_t rx_total = 0;
  uint32_t rx_match = 0;
  uint32_t rx_missed = 0;
  uint32_t rx_overrun = 0;
  uint32_t last_sample_ms = 0;
  uint32_t rx_total_prev = 0;
  uint32_t rx_match_prev = 0;
  uint32_t rx_drop_prev = 0;
  portENTER_CRITICAL(&g_state_mux);
  rx_total = s.can_stats.rx_total;
  rx_match = s.can_stats.rx_match;
  rx_missed = s.can_stats.rx_missed;
  rx_overrun = s.can_stats.rx_overrun;
  last_sample_ms = w.last_sample_ms;
  rx_total_prev = w.rx_total_prev;
  rx_match_prev = w.rx_match_prev;
  rx_drop_prev = w.rx_drop_prev;
  portEXIT_CRITICAL(&g_state_mux);
  const uint32_t drop_cur = rx_missed + rx_overrun;
  if (last_sample_ms == 0) {
    portENTER_CRITICAL(&g_state_mux);
    w.last_sample_ms = now_ms;
    w.rx_total_prev = rx_total;
    w.rx_match_prev = rx_match;
    w.rx_drop_prev = drop_cur;
    portEXIT_CRITICAL(&g_state_mux);
    return;
  }
  const uint32_t elapsed = now_ms - last_sample_ms;
  if (elapsed < 900U) {
    return;
  }
  const float scale = 1000.0f / static_cast<float>(elapsed);
  const uint32_t dt_total =
      (rx_total >= rx_total_prev) ? (rx_total - rx_total_prev) : 0;
  const uint32_t dt_match =
      (rx_match >= rx_match_prev) ? (rx_match - rx_match_prev) : 0;
  const uint32_t dt_drop = (drop_cur >= rx_drop_prev) ? (drop_cur - rx_drop_prev)
                                                      : 0;
  const float rx_per_s = static_cast<float>(dt_total) * scale;
  const float match_per_s = static_cast<float>(dt_match) * scale;
  const float drop_per_s = static_cast<float>(dt_drop) * scale;
  portENTER_CRITICAL(&g_state_mux);
  w.rx_per_s = rx_per_s;
  w.match_per_s = match_per_s;
  w.drop_per_s = drop_per_s;
  w.rx_total_prev = rx_total;
  w.rx_match_prev = rx_match;
  w.rx_drop_prev = drop_cur;
  w.last_sample_ms = now_ms;
  portEXIT_CRITICAL(&g_state_mux);
}

static void CanRxTaskEntry(void* arg) {
  (void)arg;
  const IEcuProfile& profile = g_ecu_mgr.profile();
  twai_message_t msg;
  Ms3SignalValue decoded[8];
  for (;;) {
    if (!AppConfig::kUseRealCanData || !g_state.can_ready || !g_twai.isStarted()) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    bool got_any = false;
    while (g_twai.receive(msg, 0)) {
      const uint32_t now_ms = millis();
      got_any = true;
      portENTER_CRITICAL(&g_state_mux);
      g_state.last_can_rx_ms = now_ms;
      g_state.can_stats.last_rx_ms = now_ms;
      ++g_state.can_stats.rx_total;
      ++g_state.can_stats.rx_ok_count;
      portEXIT_CRITICAL(&g_state_mux);
      if (!profile.acceptFrame(msg)) {
        continue;
      }
      portENTER_CRITICAL(&g_state_mux);
      ++g_state.can_stats.rx_match;
      g_state.last_can_match_ms = now_ms;
      portEXIT_CRITICAL(&g_state_mux);
      uint8_t count = 0;
      if (profile.decode(msg, decoded, count)) {
        portENTER_CRITICAL(&g_state_mux);
        ++g_state.can_stats.rx_dash;
        portEXIT_CRITICAL(&g_state_mux);
        const int idx = profile.dashIndexForId(msg.identifier);
        const bool idx_valid = idx >= 0;
        const bool per_id_valid = idx_valid && (idx < 5);
        const uint32_t last_id = msg.identifier;
        const uint8_t last_dlc = msg.data_length_code;
        uint8_t last_bytes[8];
        const uint8_t copy_len =
            (msg.data_length_code > sizeof(last_bytes)) ? sizeof(last_bytes)
                                                        : msg.data_length_code;
        memcpy(last_bytes, msg.data, copy_len);
        for (uint8_t i = 0; i < count; ++i) {
          if (!InRange(decoded[i].id, decoded[i].phys)) {
            portENTER_CRITICAL(&g_state_mux);
            ++g_state.can_stats.decode_oob;
            portEXIT_CRITICAL(&g_state_mux);
            g_datastore_can.note_invalid(decoded[i].id, now_ms);
            continue;
          }
          g_datastore_can.update(decoded[i].id, decoded[i].phys, now_ms);
        }
        portENTER_CRITICAL(&g_state_mux);
        if (idx_valid) {
          g_state.id_present_mask |= static_cast<uint8_t>(1U << idx);
          if (per_id_valid) {
            ++g_state.can_diag.per_id_rx[idx];
          }
        }
        g_state.can_diag.last_rx_ms = now_ms;
        g_state.can_diag.last_id = last_id;
        g_state.can_diag.last_dlc = last_dlc;
        memcpy(g_state.can_diag.last_bytes, last_bytes, copy_len);
        portEXIT_CRITICAL(&g_state_mux);
      }
    }

    uint32_t alerts = 0;
    while (g_twai.readAlerts(alerts, 0)) {
      if (alerts & TWAI_ALERT_BUS_OFF) {
        const uint32_t now_ms = millis();
        portENTER_CRITICAL(&g_state_mux);
        ++g_state.can_stats.bus_off;
        g_state.last_bus_off_ms = now_ms;
        g_state.can_ready = false;
        g_state.can_bitrate_locked = false;
        g_state.can_need_recover = true;
        g_state.can_recover_backoff_ms = 5000;
        g_state.can_recover_last_attempt_ms = now_ms;
        portEXIT_CRITICAL(&g_state_mux);
        g_twai.stop();
        g_twai.uninstall();
        pinMode(Pins::kCanTx, INPUT_PULLUP);
      }
      if (alerts & TWAI_ALERT_ERR_PASS) {
        portENTER_CRITICAL(&g_state_mux);
        ++g_state.can_stats.err_passive;
        ++g_state.can_stats.rx_err_count;
        portEXIT_CRITICAL(&g_state_mux);
      }
      if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
        portENTER_CRITICAL(&g_state_mux);
        ++g_state.can_stats.rx_overrun;
        ++g_state.can_stats.rx_drop_count;
        portEXIT_CRITICAL(&g_state_mux);
      }
    }
    twai_status_info_t status{};
    if (twai_get_status_info(&status) == ESP_OK) {
      portENTER_CRITICAL(&g_state_mux);
      g_state.can_stats.rx_missed = status.rx_missed_count;
      g_state.twai_state = status.state;
      g_state.tec = status.tx_error_counter;
      g_state.rec = status.rx_error_counter;
      g_state.twai_rx_missed = status.rx_missed_count;
      g_state.twai_last_update_ms = millis();
      portEXIT_CRITICAL(&g_state_mux);
    }
    if (!got_any) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
}

void StartCanRxTask() {
  if (g_can_rx_task_started) return;
  const BaseType_t ok = xTaskCreatePinnedToCore(
      CanRxTaskEntry, "canrx_core", 4096, nullptr, configMAX_PRIORITIES - 1,
      &g_can_rx_task, 0);
  if (ok == pdPASS) {
    g_can_rx_task_started = true;
  }
}

void CanRuntimeTick(uint32_t now_ms) {
  UpdateCanEdges(g_state, now_ms);
  if (g_can_rx_task_started) {
    UpdateCanRates(g_state, now_ms);
    UpdateCanHealth(g_state, now_ms);
    return;
  }
  if (!AppConfig::kUseRealCanData || !g_state.can_ready) {
    UpdateCanHealth(g_state, now_ms);
    return;
  }
  static bool s_boot_status_logged = false;
  const bool in_boot_window = (now_ms - g_state.boot_ms) < 3000U;

  const IEcuProfile& profile = g_ecu_mgr.profile();
  twai_message_t msg;
  Ms3SignalValue decoded[8];
  while (g_twai.receive(msg, 0)) {
    portENTER_CRITICAL(&g_state_mux);
    g_state.last_can_rx_ms = now_ms;
    g_state.can_stats.last_rx_ms = now_ms;
    ++g_state.can_stats.rx_total;
    ++g_state.can_stats.rx_ok_count;
    portEXIT_CRITICAL(&g_state_mux);
    if (!profile.acceptFrame(msg)) {
      continue;
    }
    portENTER_CRITICAL(&g_state_mux);
    ++g_state.can_stats.rx_match;
    portEXIT_CRITICAL(&g_state_mux);
    portENTER_CRITICAL(&g_state_mux);
    g_state.last_can_match_ms = now_ms;
    portEXIT_CRITICAL(&g_state_mux);
    uint8_t count = 0;
    if (profile.decode(msg, decoded, count)) {
      portENTER_CRITICAL(&g_state_mux);
      ++g_state.can_stats.rx_dash;
      portEXIT_CRITICAL(&g_state_mux);
      const int idx = profile.dashIndexForId(msg.identifier);
      uint8_t id_mask_bit = 0;
      bool per_id_valid = false;
      if (idx >= 0) {
        id_mask_bit = static_cast<uint8_t>(1U << idx);
        per_id_valid = (idx < 5);
      }
      for (uint8_t i = 0; i < count; ++i) {
        if (!InRange(decoded[i].id, decoded[i].phys)) {
          portENTER_CRITICAL(&g_state_mux);
          ++g_state.can_stats.decode_oob;
          portEXIT_CRITICAL(&g_state_mux);
#ifdef DEBUG_STALE_OLED2
          if (decoded[i].id == SignalId::kMap) {
            if (kEnableVerboseSerialLogs) {
              LOGI("[MAP] reject OOR ts=%lu val=%.3f\n",
                   static_cast<unsigned long>(now_ms),
                   static_cast<double>(decoded[i].phys));
            }
          }
#endif
          g_datastore_can.note_invalid(decoded[i].id, now_ms);
          continue;
        }
#ifdef DEBUG_STALE_OLED2
        if (decoded[i].id == SignalId::kMap) {
          if (isnan(decoded[i].phys) || isinf(decoded[i].phys)) {
            if (kEnableVerboseSerialLogs) {
              LOGI("[MAP] reject NAN/INF ts=%lu val=%.3f\n",
                   static_cast<unsigned long>(now_ms),
                   static_cast<double>(decoded[i].phys));
            }
          } else {
            if (kEnableVerboseSerialLogs) {
              LOGI("[MAP] update ts=%lu val=%.3f\n",
                   static_cast<unsigned long>(now_ms),
                   static_cast<double>(decoded[i].phys));
            }
          }
        }
#endif
        g_datastore_can.update(decoded[i].id, decoded[i].phys, now_ms);
      }
      const uint32_t last_id = msg.identifier;
      const uint8_t last_dlc = msg.data_length_code;
      uint8_t last_bytes[8];
      const uint8_t copy_len =
          (msg.data_length_code > sizeof(last_bytes)) ? sizeof(last_bytes)
                                                      : msg.data_length_code;
      memcpy(last_bytes, msg.data, copy_len);
      portENTER_CRITICAL(&g_state_mux);
      if (id_mask_bit != 0) {
        g_state.id_present_mask |= id_mask_bit;
        if (per_id_valid) {
          ++g_state.can_diag.per_id_rx[idx];
        }
      }
      g_state.can_diag.last_rx_ms = now_ms;
      g_state.can_diag.last_id = last_id;
      g_state.can_diag.last_dlc = last_dlc;
      memcpy(g_state.can_diag.last_bytes, last_bytes, copy_len);
      portEXIT_CRITICAL(&g_state_mux);
    }
  }

  uint32_t alerts = 0;
  while (g_twai.readAlerts(alerts, 0)) {
    if (in_boot_window && alerts != 0) {
      LOGI("TWAI alert=0x%08lX at %lums\n",
           static_cast<unsigned long>(alerts),
           static_cast<unsigned long>(now_ms - g_state.boot_ms));
    }
      if (alerts & TWAI_ALERT_BUS_OFF) {
        portENTER_CRITICAL(&g_state_mux);
        ++g_state.can_stats.bus_off;
        g_state.last_bus_off_ms = now_ms;
        g_state.can_ready = false;
        g_state.can_bitrate_locked = false;
        g_state.can_need_recover = true;
        g_state.can_recover_backoff_ms = 5000;
        g_state.can_recover_last_attempt_ms = now_ms;
        portEXIT_CRITICAL(&g_state_mux);
        g_twai.stop();
        g_twai.uninstall();
        pinMode(Pins::kCanTx, INPUT_PULLUP);
#if SETUP_WIZARD_ENABLED
        LOGE("CAN BUS_OFF detected, stopping and requiring wizard\r\n");
        if (!g_setup_wizard.isActive() && !g_state.ui_menu.isActive()) {
          g_setup_wizard.begin(g_state.focus_screen, g_state);
        }
#else
      LOGE("CAN BUS_OFF detected, stopping\r\n");
#endif
      return;
    }
    if (alerts & TWAI_ALERT_ERR_PASS) {
      portENTER_CRITICAL(&g_state_mux);
      ++g_state.can_stats.err_passive;
      ++g_state.can_stats.rx_err_count;
      portEXIT_CRITICAL(&g_state_mux);
    }
    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
      portENTER_CRITICAL(&g_state_mux);
      ++g_state.can_stats.rx_overrun;
      ++g_state.can_stats.rx_drop_count;
      portEXIT_CRITICAL(&g_state_mux);
    }
  }
  twai_status_info_t status{};
  if (twai_get_status_info(&status) == ESP_OK) {
    portENTER_CRITICAL(&g_state_mux);
    g_state.can_stats.rx_missed = status.rx_missed_count;
    g_state.twai_state = status.state;
    g_state.tec = status.tx_error_counter;
    g_state.rec = status.rx_error_counter;
    g_state.twai_rx_missed = status.rx_missed_count;
    g_state.twai_last_update_ms = now_ms;
    portEXIT_CRITICAL(&g_state_mux);
    if (in_boot_window && !s_boot_status_logged) {
      LOGI("TWAI status early: state=%u tec=%u rec=%u rx_missed=%lu\n",
           static_cast<unsigned>(status.state),
           static_cast<unsigned>(status.tx_error_counter),
           static_cast<unsigned>(status.rx_error_counter),
           static_cast<unsigned long>(status.rx_missed_count));
    }
  }
  if (!in_boot_window) {
    s_boot_status_logged = true;
  }
  UpdateCanRates(g_state, now_ms);
  UpdateCanHealth(g_state, now_ms);
#ifdef DEBUG_STALE_OLED2
  static uint32_t last_diag_log_ms = 0;
  if ((now_ms - last_diag_log_ms) >= 1000U) {
    last_diag_log_ms = now_ms;
    if (kEnableVerboseSerialLogs) {
      LOGI("[CANRX] t=%lu last_rx=%lu per_id=[%lu,%lu,%lu,%lu,%lu]\n",
           static_cast<unsigned long>(now_ms),
           static_cast<unsigned long>(g_state.last_can_rx_ms),
           static_cast<unsigned long>(g_state.can_diag.per_id_rx[0]),
           static_cast<unsigned long>(g_state.can_diag.per_id_rx[1]),
           static_cast<unsigned long>(g_state.can_diag.per_id_rx[2]),
           static_cast<unsigned long>(g_state.can_diag.per_id_rx[3]),
           static_cast<unsigned long>(g_state.can_diag.per_id_rx[4]));
    }
  }
#endif
}

