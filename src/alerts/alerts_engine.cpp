#include "alerts/alerts_engine.h"

#include <cmath>

#include "user_sensors/user_sensors.h"

namespace {
bool fetch(const DataStore& store, SignalId id, uint32_t now_ms, float& out) {
  const SignalRead r = store.get(id, now_ms);
  if (!r.valid) return false;
  out = r.value;
  return true;
}

int8_t pageIndexFor(PageId id) {
  size_t count = 0;
  const PageDef* pages = GetPageTable(count);
  for (size_t i = 0; i < count; ++i) {
    if (pages[i].id == id) return static_cast<int8_t>(i);
  }
  return -1;
}

Thresholds thresholdsFor(const AppState& state, PageId id) {
  Thresholds t{};
  int8_t idx = pageIndexFor(id);
  if (idx >= 0 && idx < static_cast<int8_t>(kPageCount)) {
    t = state.thresholds[idx];
  } else {
    t.min = NAN;
    t.max = NAN;
  }
  return t;
}
}  // namespace

AlertsEngine::AlertsEngine() : has_crit_(false) {}
namespace {
AlertLevel maxLevel(AlertLevel a, AlertLevel b) {
  return (static_cast<uint8_t>(b) > static_cast<uint8_t>(a)) ? b : a;
}
}  // namespace

void AlertsEngine::step(AlertState& st, bool armed, float val, float warn_on,
                        float warn_off, uint32_t warn_delay, float crit_on,
                        float crit_off, uint32_t crit_delay, bool latch_crit,
                        uint32_t now_ms, Direction dir) {
  if (!armed) {
    st.stage = AlertState::Stage::kDisarmed;
    st.level = AlertLevel::kNone;
    st.timer_ms = 0;
    return;
  }
  if (st.stage == AlertState::Stage::kDisarmed) {
    st.stage = AlertState::Stage::kArmed;
    st.timer_ms = 0;
    st.level = AlertLevel::kNone;
  }

  auto isTrip = [dir](float v, float on) {
    return dir == Direction::kHigh ? (v >= on) : (v <= on);
  };
  auto isClear = [dir](float v, float off) {
    return dir == Direction::kHigh ? (v <= off) : (v >= off);
  };

  // Pending timers
  if (st.stage == AlertState::Stage::kArmed) {
    if (isTrip(val, crit_on)) {
      st.stage = AlertState::Stage::kPending;
      st.level = AlertLevel::kCrit;
      st.timer_ms = now_ms;
    } else if (isTrip(val, warn_on)) {
      st.stage = AlertState::Stage::kPending;
      st.level = AlertLevel::kWarn;
      st.timer_ms = now_ms;
    }
  } else if (st.stage == AlertState::Stage::kPending) {
    const uint32_t elapsed = now_ms - st.timer_ms;
    if (st.level == AlertLevel::kCrit) {
      if (isTrip(val, crit_on)) {
        if (elapsed >= crit_delay) {
          st.stage = AlertState::Stage::kActive;
        }
      } else {
        st.stage = AlertState::Stage::kArmed;
        st.level = AlertLevel::kNone;
      }
    } else {
      if (isTrip(val, warn_on)) {
        if (elapsed >= warn_delay) {
          st.stage = AlertState::Stage::kActive;
        }
      } else {
        st.stage = AlertState::Stage::kArmed;
        st.level = AlertLevel::kNone;
      }
    }
  } else if (st.stage == AlertState::Stage::kActive) {
    if (st.level == AlertLevel::kCrit) {
      if (!latch_crit && isClear(val, crit_off)) {
        st.stage = AlertState::Stage::kClearing;
        st.timer_ms = now_ms;
      }
    } else if (st.level == AlertLevel::kWarn) {
      if (isClear(val, warn_off)) {
        st.stage = AlertState::Stage::kClearing;
        st.timer_ms = now_ms;
      }
    }
  } else if (st.stage == AlertState::Stage::kClearing) {
    if (st.level == AlertLevel::kCrit) {
      if (!isClear(val, crit_off)) {
        st.stage = AlertState::Stage::kActive;
      } else {
        st.level = AlertLevel::kNone;
        st.stage = AlertState::Stage::kArmed;
      }
    } else {
      if (!isClear(val, warn_off)) {
        st.stage = AlertState::Stage::kActive;
      } else {
        st.level = AlertLevel::kNone;
        st.stage = AlertState::Stage::kArmed;
      }
    }
  }
}

void AlertsEngine::resetAlert(AlertState& st) {
  st.stage = AlertState::Stage::kDisarmed;
  st.level = AlertLevel::kNone;
  st.timer_ms = 0;
}

void AlertsEngine::evalOilP(const AppState& state, const DataStore& store,
                            uint32_t now_ms) {
  const bool preset_ok =
      (state.user_sensor[0].preset == UserSensorPreset::kOilPressure) &&
      (state.user_sensor[0].kind == UserSensorKind::kPressure);
  const int8_t idx = pageIndexFor(PageId::kOilP);
  const bool min_enabled =
      (idx >= 0) && GetPageMinAlertEnabled(state, static_cast<uint8_t>(idx));
  Thresholds thr = thresholdsFor(state, PageId::kOilP);
  const bool have_min = min_enabled && !std::isnan(thr.min);
  if (!have_min || !preset_ok) {
    resetAlert(oil_p_);
    return;
  }
  float oilp = 0.0f;
  const bool ready_oil =
      state.can_ready && fetch(store, sourceToSignal(state.user_sensor[0].source),
                               now_ms, oilp);
  float rpm = 0.0f;
  bool rpm_ok = fetch(store, SignalId::kRpm, now_ms, rpm);
  float warn_on = 0.0065f * rpm + 10.0f;
  float warn_off = 0.0065f * rpm + 15.0f;
  float crit_on = 0.0065f * rpm + 5.0f;
  float crit_off = 0.0065f * rpm + 10.0f;
  if (!std::isnan(thr.min)) {
    warn_on = thr.min;
    warn_off = thr.min + 5.0f;
    crit_on = thr.min - 5.0f;
    crit_off = thr.min;
  }
  step(oil_p_, ready_oil && rpm_ok && rpm > 800.0f, oilp, warn_on, warn_off, 500,
       crit_on, crit_off, 300, true, now_ms, Direction::kLow);
}

void AlertsEngine::evalOilT(const AppState& state, const DataStore& store,
                            uint32_t now_ms) {
  const bool preset_ok =
      (state.user_sensor[1].preset == UserSensorPreset::kOilTemp) &&
      (state.user_sensor[1].kind == UserSensorKind::kTemp);
  const int8_t idx = pageIndexFor(PageId::kOilT);
  const bool max_enabled =
      (idx >= 0) && GetPageMaxAlertEnabled(state, static_cast<uint8_t>(idx));
  Thresholds thr = thresholdsFor(state, PageId::kOilT);
  const bool have_max = max_enabled && !std::isnan(thr.max);
  if (!have_max || !preset_ok) {
    resetAlert(oil_t_);
    return;
  }
  float oilt = 0.0f;
  const bool ready = state.can_ready &&
                     fetch(store, sourceToSignal(state.user_sensor[1].source),
                           now_ms, oilt);
  float warn_on = 130.0f;
  float warn_off = 125.0f;
  float crit_on = 140.0f;
  float crit_off = 135.0f;
  if (!std::isnan(thr.max)) {
    warn_on = thr.max;
    warn_off = thr.max - 5.0f;
    crit_on = thr.max + 10.0f;
    crit_off = thr.max + 5.0f;
  }
  step(oil_t_, ready, oilt, warn_on, warn_off, 1000, crit_on, crit_off, 1500,
       false, now_ms, Direction::kHigh);
}

void AlertsEngine::evalBatt(const AppState& state, const DataStore& store,
                            uint32_t now_ms) {
  const int8_t idx = pageIndexFor(PageId::kBatt);
  const bool min_enabled =
      (idx >= 0) && GetPageMinAlertEnabled(state, static_cast<uint8_t>(idx));
  const bool max_enabled =
      (idx >= 0) && GetPageMaxAlertEnabled(state, static_cast<uint8_t>(idx));
  float batt = 0.0f;
  float rpm = 0.0f;
  const bool ready = state.can_ready && fetch(store, SignalId::kBatt, now_ms, batt) &&
                     fetch(store, SignalId::kRpm, now_ms, rpm) && rpm > 800.0f;
  Thresholds thr = thresholdsFor(state, PageId::kBatt);
  const bool have_min = min_enabled && !std::isnan(thr.min);
  const bool have_max = max_enabled && !std::isnan(thr.max);
  if (!have_min && !have_max) {
    resetAlert(batt_);
    return;
  }
  float warn_on_low = 12.0f;
  float warn_off_low = 12.5f;
  float crit_on_low = 11.5f;
  float crit_off_low = 12.0f;
  if (have_min) {
    warn_on_low = thr.min;
    warn_off_low = thr.min + 0.5f;
    crit_on_low = thr.min - 0.5f;
    crit_off_low = thr.min;
  }
  if (have_min) {
    step(batt_, ready, batt, warn_on_low, warn_off_low, 1000, crit_on_low,
         crit_off_low, 500, false, now_ms, Direction::kLow);
  }
  float warn_on_high = 15.0f;
  float warn_off_high = 14.8f;
  float crit_on_high = 16.0f;
  float crit_off_high = 15.5f;
  if (have_max) {
    warn_on_high = thr.max;
    warn_off_high = thr.max - 0.2f;
    crit_on_high = thr.max + 0.5f;
    crit_off_high = thr.max;
  }
  if (have_max && ready) {
    if (batt > crit_on_high) {
      batt_.level = AlertLevel::kCrit;
    } else if (batt > warn_on_high && batt_.level != AlertLevel::kCrit) {
      batt_.level = AlertLevel::kWarn;
    } else if (batt < warn_off_high && batt_.level == AlertLevel::kWarn) {
      batt_.level = AlertLevel::kNone;
    } else if (batt < crit_off_high && batt_.level == AlertLevel::kCrit) {
      batt_.level = AlertLevel::kNone;
    }
  }
}

void AlertsEngine::evalKnk(const AppState& state, const DataStore& store,
                           uint32_t now_ms) {
  const int8_t idx = pageIndexFor(PageId::kKnk);
  const bool max_enabled =
      (idx >= 0) && GetPageMaxAlertEnabled(state, static_cast<uint8_t>(idx));
  Thresholds thr = thresholdsFor(state, PageId::kKnk);
  const bool have_max = max_enabled && !std::isnan(thr.max);
  if (!have_max) {
    resetAlert(knk_);
    return;
  }
  float knk = 0.0f;
  const bool ready = state.can_ready && fetch(store, SignalId::kKnkRetard, now_ms, knk);
  float warn_on = 3.0f;
  float warn_off = 2.0f;
  float crit_on = 6.0f;
  float crit_off = 4.0f;
  if (!std::isnan(thr.max)) {
    warn_on = thr.max;
    warn_off = thr.max - 1.0f;
    crit_on = thr.max + 2.0f;
    crit_off = thr.max + 1.0f;
  }
  step(knk_, ready, knk, warn_on, warn_off, 500, crit_on, crit_off, 300, false,
       now_ms, Direction::kHigh);
}

void AlertsEngine::update(const AppState& state, const DataStore& store,
                          uint32_t now_ms) {
  for (size_t i = 0; i < kPageCount; ++i) {
    page_level_[i] = AlertLevel::kNone;
  }
  size_t count = 0;
  const PageDef* pages = GetPageTable(count);
  const size_t n = (count < kPageCount) ? count : kPageCount;
  for (size_t i = 0; i < n; ++i) {
    Thresholds thr = state.thresholds[i];
    if (!GetPageMinAlertEnabled(state, static_cast<uint8_t>(i))) {
      thr.min = NAN;
    }
    if (!GetPageMaxAlertEnabled(state, static_cast<uint8_t>(i))) {
      thr.max = NAN;
    }
    if (std::isnan(thr.min) && std::isnan(thr.max)) {
      continue;
    }
    float canon = 0.0f;
    if (!PageCanonicalValue(
            pages[i].id, state,
            DisplayConfigForDisplay(state, PhysicalDisplayId::kPrimary),
            store, now_ms, canon)) {
      continue;
    }
    const bool low = !std::isnan(thr.min) && canon < thr.min;
    const bool high = !std::isnan(thr.max) && canon > thr.max;
    if (low || high) {
      page_level_[i] = AlertLevel::kCrit;
    }
  }
  has_crit_ = false;
  evalOilP(state, store, now_ms);
  evalOilT(state, store, now_ms);
  evalBatt(state, store, now_ms);
  evalKnk(state, store, now_ms);
  auto mergeLevel = [&](PageId pid, AlertLevel lvl) {
    int8_t idx = pageIndexFor(pid);
    if (idx >= 0 && idx < static_cast<int8_t>(kPageCount)) {
      page_level_[idx] = maxLevel(page_level_[idx], lvl);
    }
  };
  mergeLevel(PageId::kOilP, oil_p_.level);
  mergeLevel(PageId::kOilT, oil_t_.level);
  mergeLevel(PageId::kBatt, batt_.level);
  mergeLevel(PageId::kKnk, knk_.level);
  for (size_t i = 0; i < kPageCount; ++i) {
    if (page_level_[i] == AlertLevel::kCrit) {
      has_crit_ = true;
      break;
    }
  }
}

AlertLevel AlertsEngine::alertForPage(PageId page) const {
  int8_t idx = pageIndexFor(page);
  if (idx >= 0 && idx < static_cast<int8_t>(kPageCount)) {
    return page_level_[idx];
  }
  return AlertLevel::kNone;
}
