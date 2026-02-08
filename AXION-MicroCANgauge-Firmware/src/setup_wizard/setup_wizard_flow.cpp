#include "app_config.h"
#if SETUP_WIZARD_ENABLED
// SetupWizard flow/state machine: phases, timing, blip/baro handling.
#include "setup_wizard/setup_wizard.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/app_globals.h"
#include "config/logging.h"
#include "ecu/ecu_manager.h"

extern EcuManager g_ecu_mgr;

namespace {

uint16_t clamp16(uint32_t v, uint16_t lo, uint16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return static_cast<uint16_t>(v);
}

uint32_t medianOfArray(const uint16_t* buf, uint8_t count) {
  uint16_t tmp[16];
  for (uint8_t i = 0; i < count; ++i) {
    tmp[i] = buf[i];
  }
  for (uint8_t i = 0; i < count; ++i) {
    for (uint8_t j = i + 1; j < count; ++j) {
      if (tmp[j] < tmp[i]) {
        const uint16_t t = tmp[i];
        tmp[i] = tmp[j];
        tmp[j] = t;
      }
    }
  }
  return tmp[count / 2];
}

}  // namespace

SetupWizard::SetupWizard(TwaiLink& link, DataStore& store, NvsStore& nvs)
    : twai_(link),
      store_(store),
      nvs_(nvs),
      phase_(Phase::kInactive),
      phase_start_ms_(0),
      focus_screen_(0),
      scan_attempted_(false),
      intervals_done_(false),
      baro_acquired_(false),
      baro_kpa_(0.0f),
      last_map_kpa_(0.0f),
      last_rpm_(0.0f),
      last_tps_(0.0f),
      last_map_ms_(0),
      last_rpm_ms_(0),
      last_tps_ms_(0),
      blip_detected_(false),
      map_count_(0),
      map_head_(0),
      stable_start_ms_(0),
      stable_cond_(false),
      baro_avg_start_ms_(0),
      baro_sum_(0.0f),
      baro_count_(0),
      rpm_count_(0),
      map_count_run_(0),
      rpm_head_(0),
      map_head_run_(0) {
  memset(stale_ms_, 0, sizeof(stale_ms_));
  memset(intervals_, 0, sizeof(intervals_));
  memset(map_samples_, 0, sizeof(map_samples_));
  memset(rpm_buf_, 0, sizeof(rpm_buf_));
  memset(map_buf_, 0, sizeof(map_buf_));
}

bool SetupWizard::isActive() const { return phase_ != Phase::kInactive; }

void SetupWizard::begin(uint8_t focus_screen, AppState& state) {
  focus_screen_ = focus_screen;
  scan_attempted_ = false;
  intervals_done_ = false;
  baro_acquired_ = false;
  baro_kpa_ = 0.0f;
  memset(stale_ms_, 0, sizeof(stale_ms_));
  memset(intervals_, 0, sizeof(intervals_));
  last_map_ms_ = last_rpm_ms_ = last_tps_ms_ = 0;
  map_count_ = 0;
  map_head_ = 0;
  stable_start_ms_ = 0;
  stable_cond_ = false;
  baro_avg_start_ms_ = 0;
  baro_sum_ = 0.0f;
  baro_count_ = 0;
  rpm_count_ = map_count_run_ = 0;
  rpm_head_ = map_head_run_ = 0;
  blip_detected_ = false;
  scan_rate_idx_ = 0;
  locked_rate_ = 0;
  validate_active_ = false;
  validate_start_ms_ = 0;
  validate_window_ms_ = 0;
  validate_rx_total_ = validate_rx_dash_ = 0;
  validate_err_bus_off_ = validate_err_ep_ = validate_err_ov_ = 0;
  validate_err_missed_ = 0;
  validate_rate_ = 0;
  debug_view_ = false;
  debug_last_id_ = 0;
  debug_last_dlc_ = 0;
  debug_rx_total_ = 0;
  debug_fps_counter_ = 0;
  debug_fps_ = 0;
  debug_last_fps_ts_ = 0;

  if (!AppConfig::IsRealCanEnabled()) {
    state.wizard_active = false;
    phase_ = Phase::kInactive;
    LOGI("[WIZ] skipped (simulation mode)\r\n");
    if (state.oled_primary_ready) {
      g_oled_primary.drawLines("SIM MODE", "CAN SCAN SKIPPED", nullptr, nullptr);
    }
    return;
  }

  stopCan(state);

  SetupPersist persist{};
  nvs_.loadSetupPersist(persist);
  for (uint8_t i = 0; i < 5; ++i) {
    stale_ms_[i] = persist.stale_ms[i];
  }
  baro_acquired_ = persist.baro_acquired;
  baro_kpa_ = persist.baro_kpa;

  if (persist.phase >= static_cast<uint8_t>(SetupPersist::Phase::kRunDone)) {
    phase_ = Phase::kSetupDone;
  } else if (persist.phase >=
             static_cast<uint8_t>(SetupPersist::Phase::kKoeoDone)) {
    phase_ = Phase::kRunIntro;
  } else {
    phase_ = Phase::kKoeoScanMenu;
  }
  phase_start_ms_ = millis();
  state.wizard_active = true;
}

void SetupWizard::handleAction(UiAction action, AppState& state) {
  if (!isActive()) {
    return;
  }
  if (action == UiAction::kClick5) {
    debug_view_ = !debug_view_;
    return;
  }
  if (action == UiAction::kClick3 || action == UiAction::kClick1Long) {
    stopCan(state);
    state.wizard_active = false;
    phase_ = Phase::kInactive;
    return;
  }
  if (phase_ == Phase::kKoeoScanMenu) {
    if (action == UiAction::kClick2) {  // auto-scan
      stopCan(state);
      scan_attempted_ = false;
      fail_reason_ = FailReason::kNone;
      changePhase(Phase::kKoeoScanRun, millis());
    } else if (action == UiAction::kClick1) {  // manual select
      changePhase(Phase::kKoeoManualBitrate, millis());
    }
  } else if (phase_ == Phase::kKoeoManualBitrate) {
    uint8_t count = 0;
    const uint32_t* rates = g_ecu_mgr.profile().scanBitrates(count);
    if (count == 0) {
      changePhase(Phase::kKoeoScanMenu, millis());
      return;
    }
    if (action == UiAction::kClick1) {
      scan_rate_idx_ = static_cast<uint8_t>((scan_rate_idx_ + 1) % count);
    } else if (action == UiAction::kClick1Long) {
      scan_rate_idx_ =
          static_cast<uint8_t>((scan_rate_idx_ + count - 1) % count);
    } else if (action == UiAction::kClick2) {
      state.can_bitrate_value = rates[scan_rate_idx_];
      stopCan(state);
      fail_reason_ = FailReason::kNone;
      changePhase(Phase::kKoeoValidateBitrate, millis());
    } else if (action == UiAction::kClick3) {
      stopCan(state);
      state.wizard_active = false;
      phase_ = Phase::kInactive;
    }
  } else if (phase_ == Phase::kKoeoScanRun) {
    if (action == UiAction::kClick1) {
      scan_attempted_ = false;
      changePhase(Phase::kKoeoScanRun, millis());
    } else if (action == UiAction::kClick2) {
      changePhase(Phase::kKoeoScanMenu, millis());
    }
  } else if (phase_ == Phase::kKoeoScanLocked) {
    if (action == UiAction::kClick2) {
      // explicit save & reboot
      CanSettings locked{};
      locked.schema_version = 1;
      locked.bitrate_locked = true;
      locked.bitrate_value = locked_rate_;
      locked.id_present_mask = state.id_present_mask;
      locked.hash_match = true;
      locked.ecu_profile_id = 1;
      if (nvs_.saveCanSettings(locked)) {
        state.can_bitrate_locked = true;
        state.can_bitrate_value = locked_rate_;
        changePhase(Phase::kKoeoScanSaved, millis());
        ESP.restart();
      } else {
        fail_reason_ = FailReason::kLowScore;  // reuse bucket for save failure
        changePhase(Phase::kKoeoFail, millis());
      }
    } else if (action == UiAction::kClick1) {
      changePhase(Phase::kKoeoScanMenu, millis());
    }
  } else if (phase_ == Phase::kRunFail && action == UiAction::kClick1) {
    changePhase(Phase::kRunIntro, millis());
  }
}

void SetupWizard::changePhase(Phase p, uint32_t now_ms) {
  phase_ = p;
  phase_start_ms_ = now_ms;
  if (p == Phase::kKoeoScanRun) {
    scan_attempted_ = false;
  }
  if (p != Phase::kKoeoValidateBitrate && p != Phase::kKoeoScanRun) {
    validate_active_ = false;
    validate_start_ms_ = 0;
    validate_window_ms_ = 0;
  }
  if (p == Phase::kKoeoBaro) {
    resetBaroBuffers();
  } else if (p == Phase::kRunCapture) {
    resetRunBuffers();
    blip_detected_ = false;
  }
  LOGI("[WIZ] phase -> %u\r\n", static_cast<unsigned>(p));
}

void SetupWizard::recordInterval(uint8_t idx, uint32_t ts_ms) {
  if (idx >= 5) return;
  IntervalBuf& buf = intervals_[idx];
  if (buf.last_ts == 0) {
    buf.last_ts = ts_ms;
    return;
  }
  const uint32_t delta = ts_ms - buf.last_ts;
  buf.last_ts = ts_ms;
  if (buf.count < 16) {
    buf.deltas[buf.count++] = static_cast<uint16_t>(delta);
  }
}

bool SetupWizard::intervalsReady(uint8_t idx) const {
  if (idx >= 5) return false;
  return intervals_[idx].count >= 10;
}

uint32_t SetupWizard::medianInterval(uint8_t idx) const {
  if (!intervalsReady(idx)) return 0;
  return medianOfArray(intervals_[idx].deltas, intervals_[idx].count);
}

bool SetupWizard::allIntervalsDone(uint8_t mask) const {
  for (uint8_t i = 0; i < 5; ++i) {
    if (mask & (1U << i)) {
      if (!intervalsReady(i)) return false;
    }
  }
  return true;
}

void SetupWizard::finalizeIntervals(AppState& state) {
  uint8_t mask = state.id_present_mask;
  if (mask == 0) return;
  const IEcuProfile& profile = g_ecu_mgr.profile();
  const uint8_t dash_count =
      std::min<uint8_t>(profile.dashIdCount(), static_cast<uint8_t>(5));
  for (uint8_t i = 0; i < dash_count; ++i) {
    if (mask & (1U << i)) {
      uint32_t med = medianInterval(i);
      uint32_t stale = med * 4U;
      stale_ms_[i] = clamp16(stale, 200, 1000);
      SignalSpan span = profile.dashSignalsForIndex(i);
      store_.setStaleForSignals(span.ids, span.count, stale_ms_[i]);
    } else {
      stale_ms_[i] = 500;
    }
  }
  saveProgressKoeo(state, false);
  intervals_done_ = true;
}

void SetupWizard::stopCan(AppState& state) {
  twai_.stop();
  twai_.uninstall();
  state.can_ready = false;
  pinMode(Pins::kCanTx, INPUT_PULLUP);
}

void SetupWizard::resetBaroBuffers() {
  map_count_ = 0;
  map_head_ = 0;
  stable_start_ms_ = 0;
  stable_cond_ = false;
  baro_avg_start_ms_ = 0;
  baro_sum_ = 0.0f;
  baro_count_ = 0;
}

float SetupWizard::computeMean() const {
  if (map_count_ == 0) return 0.0f;
  float sum = 0.0f;
  for (uint8_t i = 0; i < map_count_; ++i) {
    sum += map_samples_[i];
  }
  return sum / static_cast<float>(map_count_);
}

float SetupWizard::computeSigma() const {
  if (map_count_ < 2) return 1000.0f;
  const float mean = computeMean();
  float acc = 0.0f;
  for (uint8_t i = 0; i < map_count_; ++i) {
    const float d = map_samples_[i] - mean;
    acc += d * d;
  }
  return sqrtf(acc / static_cast<float>(map_count_));
}

void SetupWizard::handleBaro(uint32_t now_ms, AppState& state) {
  if (last_map_ms_ == 0) return;
  const uint32_t age_map = now_ms - last_map_ms_;
  const bool rpm_present = last_rpm_ms_ != 0;
  const uint32_t age_rpm = rpm_present ? (now_ms - last_rpm_ms_) : 0;
  const uint32_t age_tps = last_tps_ms_ ? (now_ms - last_tps_ms_) : 0xFFFFFFFFu;
  if (age_map > 1000 || (rpm_present && age_rpm > 1000)) {
    return;
  }
  const bool rpm_ok = (!rpm_present) || (last_rpm_ < 50.0f);
  const bool tps_valid = age_tps < 1000;
  const bool tps_ok = (!tps_valid) || (last_tps_ < 1.0f);
  if (rpm_ok && tps_ok) {
    if (!stable_cond_) {
      stable_start_ms_ = now_ms;
      stable_cond_ = true;
    }
  } else {
    stable_cond_ = false;
    stable_start_ms_ = 0;
    map_count_ = 0;
    map_head_ = 0;
    baro_avg_start_ms_ = 0;
    baro_sum_ = 0.0f;
    baro_count_ = 0;
    return;
  }

  if (stable_cond_) {
    if (map_count_ < 20) {
      map_samples_[map_count_++] = last_map_kpa_;
    } else {
      map_samples_[map_head_] = last_map_kpa_;
      map_head_ = (map_head_ + 1) % 20;
    }
  }

  if (stable_cond_ && (now_ms - stable_start_ms_) >= 1500 &&
      map_count_ >= 20 && computeSigma() < 0.30f) {
    if (baro_avg_start_ms_ == 0) {
      baro_avg_start_ms_ = now_ms;
      baro_sum_ = 0.0f;
      baro_count_ = 0;
    }
    baro_sum_ += last_map_kpa_;
    ++baro_count_;
    if ((now_ms - baro_avg_start_ms_) >= 1000 && baro_count_ > 0) {
      baro_kpa_ = baro_sum_ / static_cast<float>(baro_count_);
      baro_acquired_ = true;
      saveProgressKoeo(state, false);
    }
  }
}

void SetupWizard::resetRunBuffers() {
  memset(rpm_buf_, 0, sizeof(rpm_buf_));
  memset(map_buf_, 0, sizeof(map_buf_));
  rpm_count_ = map_count_run_ = 0;
  rpm_head_ = map_head_run_ = 0;
}

void SetupWizard::pushSample(SampleBuf* buf, uint8_t capacity, uint8_t& head,
                             uint8_t& count, uint32_t ts, float val) {
  buf[head].ts = ts;
  buf[head].val = val;
  head = (head + 1) % capacity;
  if (count < capacity) {
    ++count;
  }
}

bool SetupWizard::computeDelta(const SampleBuf* buf, uint8_t count,
                               uint32_t now_ms, float& delta,
                               uint32_t window_ms) const {
  if (count == 0) {
    delta = 0.0f;
    return false;
  }
  float min_v = 1e9f;
  float max_v = -1e9f;
  bool any = false;
  for (uint8_t i = 0; i < count; ++i) {
    const uint32_t age = now_ms - buf[i].ts;
    if (age <= window_ms) {
      if (buf[i].val < min_v) min_v = buf[i].val;
      if (buf[i].val > max_v) max_v = buf[i].val;
      any = true;
    }
  }
  if (!any) {
    delta = 0.0f;
    return false;
  }
  delta = max_v - min_v;
  return true;
}

void SetupWizard::recordDebug(const twai_message_t& msg, uint32_t now_ms) {
  debug_last_id_ = msg.identifier;
  debug_last_dlc_ = msg.data_length_code;
  ++debug_rx_total_;
  ++debug_fps_counter_;
  if (debug_last_fps_ts_ == 0) {
    debug_last_fps_ts_ = now_ms;
  }
  if ((now_ms - debug_last_fps_ts_) >= 1000) {
    debug_fps_ = debug_fps_counter_;
    debug_fps_counter_ = 0;
    debug_last_fps_ts_ = now_ms;
  }
}

void SetupWizard::handleRun(uint32_t now_ms) {
  if (last_rpm_ms_ == 0 || last_map_ms_ == 0) {
    return;
  }
  pushSample(rpm_buf_, 24, rpm_head_, rpm_count_, last_rpm_ms_, last_rpm_);
  pushSample(map_buf_, 24, map_head_run_, map_count_run_, last_map_ms_,
             last_map_kpa_);

  float d_rpm = 0.0f;
  float d_map = 0.0f;
  const bool rpm_ok = computeDelta(rpm_buf_, rpm_count_, now_ms, d_rpm, 1200);
  const bool map_ok = computeDelta(map_buf_, map_count_run_, now_ms, d_map, 1200);
  if (rpm_ok && map_ok && d_rpm >= 300.0f && d_map >= 1.0f) {
    blip_detected_ = true;
    changePhase(Phase::kRunValidate, now_ms);
  }
}

void SetupWizard::tick(AppState& state, uint32_t now_ms) {
  if (!isActive()) {
    return;
  }

  switch (phase_) {
    case Phase::kKoeoIntro:
      if (!AppConfig::IsRealCanEnabled()) {
        state.wizard_active = false;
        phase_ = Phase::kInactive;
      } else if ((now_ms - phase_start_ms_) >= 500) {
        changePhase(Phase::kKoeoScanMenu, now_ms);
      }
      break;
    case Phase::kKoeoScanMenu:
      // Wait for user action (Click2 auto-scan, Click1 manual)
      break;
    case Phase::kKoeoValidateBitrate: {
      if (!state.can_ready) {
        if (!validate_active_) {
          uint32_t rate = state.can_bitrate_value;
          if (rate == 0) {
            rate = g_ecu_mgr.profile().preferredBitrate();
            state.can_bitrate_value = rate;
          }
          twai_.stop();
          twai_.uninstall();
          if (!twai_.startNormal(rate)) {
            last_diag_ = {};
            last_diag_.reason = AutoBaudResult::kConfirmFailed;
            last_diag_.entry_count = 1;
            last_diag_.entries[0].bitrate = rate;
            fail_reason_ = FailReason::kCanStartFail;
            changePhase(Phase::kKoeoFail, now_ms);
            stopCan(state);
            break;
          }
          const ValidationSpec& vs = g_ecu_mgr.profile().validationSpec();
          // manual validate: keep window short and decisive
          validate_window_ms_ = (vs.window_ms > 0) ? vs.window_ms : 800;
          validate_start_ms_ = now_ms;
          validate_rx_total_ = 0;
          validate_rx_dash_ = 0;
          validate_err_bus_off_ = 0;
          validate_err_ep_ = 0;
          validate_err_ov_ = 0;
          validate_err_missed_ = 0;
          validate_rate_ = rate;
          validate_active_ = true;
        }
        // Incremental sampling
        twai_message_t msg{};
        while (twai_.receive(msg, 0)) {
          ++validate_rx_total_;
          recordDebug(msg, now_ms);
          const int idx = g_ecu_mgr.profile().dashIndexForId(msg.identifier);
          if (idx >= 0) {
            ++validate_rx_dash_;
            state.id_present_mask |= (1u << idx);
          }
        }
        uint32_t alerts = 0;
        while (twai_.readAlerts(alerts, 0)) {
          if (alerts & TWAI_ALERT_BUS_OFF) ++validate_err_bus_off_;
          if (alerts & TWAI_ALERT_ERR_PASS) ++validate_err_ep_;
          if (alerts & TWAI_ALERT_RX_QUEUE_FULL) ++validate_err_ov_;
        }
        if ((now_ms - validate_start_ms_) >= validate_window_ms_) {
          twai_status_info_t st{};
          if (twai_get_status_info(&st) == ESP_OK) {
            validate_err_missed_ = st.rx_missed_count;
          }
          const ValidationSpec& vs = g_ecu_mgr.profile().validationSpec();
          const uint16_t min_dash =
              (vs.min_rx_dash > 0) ? vs.min_rx_dash : 1;  // manual: at least 1 frame
          const bool ok =
              (validate_rx_total_ > 0) && (validate_rx_dash_ >= min_dash) &&
              (validate_err_bus_off_ == 0) && (validate_err_ep_ == 0) &&
              (validate_err_ov_ == 0) && (validate_err_missed_ <= 2);
          if (!ok) {
            last_diag_ = {};
            last_diag_.reason = AutoBaudResult::kConfirmFailed;  // validate_fail
            if (validate_rx_total_ == 0) {
              fail_reason_ = FailReason::kNoFrames;
            } else if (validate_err_bus_off_ || validate_err_ep_ ||
                       validate_err_ov_) {
              fail_reason_ = FailReason::kBusError;
            } else {
              fail_reason_ = FailReason::kLowScore;
            }
            last_diag_.entry_count = 1;
            last_diag_.entries[0].bitrate = validate_rate_;
            last_diag_.entries[0].rx_total = validate_rx_total_;
            last_diag_.entries[0].expected_ids_hits = validate_rx_dash_;
            last_diag_.entries[0].errors =
                validate_err_bus_off_ + validate_err_ep_ + validate_err_ov_;
            last_diag_.entries[0].missed = validate_err_missed_;
            last_diag_.entries[0].overruns = validate_err_ov_;
            stopCan(state);
            changePhase(Phase::kKoeoFail, now_ms);
          } else {
            state.can_ready = true;
            state.can_bitrate_locked = true;
            locked_rate_ = validate_rate_;
            changePhase(Phase::kKoeoScanLocked, now_ms);
          }
          validate_active_ = false;
        }
      }
      break;
    }
    case Phase::kKoeoScanRun: {
      if (!scan_attempted_ && AppConfig::IsRealCanEnabled()) {
        scan_attempted_ = true;
        AutoBaudDiag diag{};
        diag.reason = AutoBaudResult::kNoActivity;
        last_diag_ = {};
        const uint32_t* rates = nullptr;
        uint8_t rate_count = 0;
        rates = g_ecu_mgr.profile().scanBitrates(rate_count);
        if (!rates || rate_count == 0) {
          changePhase(Phase::kKoeoFail, now_ms);
          stopCan(state);
          break;
        }
        AutoBaudStats best_stats{};
        CanSettings best_lock{};
        int best_idx = -1;
        uint32_t best_score = 0;
        const uint32_t window_ms = 400;
        for (uint8_t i = 0; i < rate_count; ++i) {
          const uint32_t rate = rates[i];
          twai_.stop();
          twai_.uninstall();
          if (!twai_.startListenOnly(rate)) {
            continue;
          }
          AutoBaudStats stats{};
          uint32_t start_ms = now_ms;
          uint32_t alerts = 0;
          twai_message_t msg{};
          while ((millis() - start_ms) < window_ms) {
            while (twai_.receive(msg, 0)) {
              ++stats.rx_total;
              recordDebug(msg, millis());
              const int idx = g_ecu_mgr.profile().dashIndexForId(msg.identifier);
              if (idx >= 0) {
                ++stats.rx_dash;
                stats.id_present_mask |= (1u << idx);
              }
            }
          while (twai_.readAlerts(alerts, 0)) {
            if (alerts & TWAI_ALERT_BUS_OFF) stats.bus_off++;
            if (alerts & TWAI_ALERT_ERR_PASS) stats.err_passive++;
            if (alerts & TWAI_ALERT_RX_QUEUE_FULL) stats.rx_overrun++;
          }
        }
          twai_status_info_t st{};
          if (twai_get_status_info(&st) == ESP_OK) {
            stats.rx_missed = st.rx_missed_count;
          }
          twai_.stop();
          twai_.uninstall();
          uint32_t score = stats.rx_total;
          if (stats.rx_dash > 0) score += (stats.rx_dash * 4);
          if (stats.bus_off || stats.err_passive || stats.rx_overrun) score = 0;
          AutoBaudScore entry{};
          entry.bitrate = rate;
          entry.rx_total = stats.rx_total;
          entry.expected_ids_hits = stats.rx_dash;
          uint8_t distinct = 0;
          for (uint8_t b = 0; b < 5; ++b) {
            if (stats.id_present_mask & (1u << b)) ++distinct;
          }
          entry.distinct_ids = distinct;
          entry.errors = stats.bus_off + stats.err_passive + stats.rx_overrun;
          entry.missed = stats.rx_missed;
          entry.overruns = stats.rx_overrun;
          entry.score = static_cast<int32_t>(score);
          if (last_diag_.entry_count < AutoBaudDiag::kMaxEntries) {
            last_diag_.entries[last_diag_.entry_count++] = entry;
          }
          if (score > best_score) {
            best_score = score;
            best_stats = stats;
            best_lock.bitrate_locked = true;
            best_lock.bitrate_value = rate;
            best_lock.id_present_mask = stats.id_present_mask;
            best_lock.hash_match = true;
            best_idx = i;
          }
        }
        if (best_idx >= 0 && best_score > 0) {
          last_diag_.reason = AutoBaudResult::kOk;
          last_scan_ = best_stats;
          locked_rate_ = best_lock.bitrate_value;
          state.id_present_mask = best_lock.id_present_mask;
          changePhase(Phase::kKoeoScanLocked, now_ms);
        } else {
          last_diag_.reason = AutoBaudResult::kNoActivity;
          fail_reason_ = FailReason::kNoFrames;
          changePhase(Phase::kKoeoFail, now_ms);
        }
        stopCan(state);
      }
      break;
    }
    case Phase::kKoeoScanLocked:
      // Wait for user to save/reboot or back out (handled in handleAction)
      break;
    case Phase::kKoeoScanSaved:
      // We reboot immediately after save; nothing to do
      break;
    case Phase::kKoeoManualBitrate:
      break;
    case Phase::kKoeoMeasurePeriod:
      if (state.can_ready) {
        drainCan(state, now_ms);
        if (!intervals_done_ && allIntervalsDone(state.id_present_mask)) {
          finalizeIntervals(state);
          changePhase(Phase::kKoeoBaro, now_ms);
        } else if ((now_ms - phase_start_ms_) > 8000) {
          changePhase(Phase::kKoeoFail, now_ms);
          stopCan(state);
        }
      }
      break;
    case Phase::kKoeoBaro:
      if (state.can_ready) {
        drainCan(state, now_ms);
        handleBaro(now_ms, state);
        if ((now_ms - phase_start_ms_) > 12000 && !baro_acquired_) {
          changePhase(Phase::kKoeoFail, now_ms);
          stopCan(state);
        }
        if (baro_acquired_) {
          changePhase(Phase::kKoeoValidate, now_ms);
        }
      }
      break;
    case Phase::kKoeoValidate:
      if (baro_acquired_ && intervals_done_ && state.id_present_mask != 0) {
        saveProgressKoeo(state, true);
        changePhase(Phase::kKoeoDone, now_ms);
      } else if ((now_ms - phase_start_ms_) > 14000) {
        changePhase(Phase::kKoeoFail, now_ms);
        stopCan(state);
      }
      break;
    case Phase::kKoeoDone:
      if ((now_ms - phase_start_ms_) > 700) {
        changePhase(Phase::kRunIntro, now_ms);
      }
      break;
    case Phase::kRunIntro:
      if (!AppConfig::IsRealCanEnabled()) {
        state.wizard_active = false;
        phase_ = Phase::kInactive;
      } else if ((now_ms - phase_start_ms_) > 600) {
        changePhase(Phase::kRunCapture, now_ms);
      }
      break;
    case Phase::kRunCapture:
      if (state.can_ready && AppConfig::IsRealCanEnabled()) {
        drainCan(state, now_ms);
        handleRun(now_ms);
        if ((now_ms - phase_start_ms_) > 15000) {
          changePhase(Phase::kRunFail, now_ms);
          stopCan(state);
        }
      }
      break;
    case Phase::kRunValidate:
      if (blip_detected_) {
        changePhase(Phase::kSetupDone, now_ms);
      } else if ((now_ms - phase_start_ms_) > 2000) {
        changePhase(Phase::kRunFail, now_ms);
        stopCan(state);
      }
      break;
    case Phase::kSetupDone:
      saveProgressRun(state);
      state.wizard_active = false;
      phase_ = Phase::kInactive;
      break;
    case Phase::kKoeoFail:
    case Phase::kRunFail:
      if ((now_ms - phase_start_ms_) > 20000) {
        stopCan(state);
        state.wizard_active = false;
        phase_ = Phase::kInactive;
      }
      break;
    case Phase::kInactive:
    default:
      break;
  }
}

#endif  // SETUP_WIZARD_ENABLED
