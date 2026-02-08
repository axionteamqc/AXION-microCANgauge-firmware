#include "ui/pages.h"

#include <cmath>
#include "app_config.h"
#include "config/logging.h"
#include "user_sensors/user_sensors.h"

namespace {

constexpr float kKpaToPsi = 0.1450377377f;
constexpr float kMsToKmh = 3.6f;
constexpr float kMsToMph = 2.2369362921f;

float f_to_c(float f) { return (f - 32.0f) * (5.0f / 9.0f); }
float c_to_f(float c) { return (c * (9.0f / 5.0f)) + 32.0f; }

void MarkStale(PageRenderData& d) {
  d.has_error = true;
  strlcpy(d.err_a, "STAL", sizeof(d.err_a));
  d.valid = false;
  d.has_canon = false;
}

void MarkInvalid(PageRenderData& d) {
  d.has_error = true;
  strlcpy(d.err_a, "INV", sizeof(d.err_a));
  d.unit = "";
  d.valid = false;
  d.has_canon = false;
}

bool CanLinkLive(const AppState& state, uint32_t now_ms) {
  if (state.demo_mode || !AppConfig::IsRealCanEnabled() || !state.can_ready) {
    return false;
  }
  if (state.can_link.health != CanHealth::kOk) return false;
  if (state.last_can_rx_ms == 0) return false;
  const uint32_t elapsed = (now_ms >= state.last_can_rx_ms)
                               ? (now_ms - state.last_can_rx_ms)
                               : ((state.last_can_rx_ms - now_ms) <= 2000U
                                      ? 0
                                      : (now_ms - state.last_can_rx_ms));
  return elapsed < 1200U;
}

void formatInt(char* buf, size_t len, uint32_t v) {
  snprintf(buf, len, "%lu", static_cast<unsigned long>(v));
}

void formatFloat1(char* buf, size_t len, float v) {
  snprintf(buf, len, "%.1f", static_cast<double>(v));
}

static bool UseLastGood(const AppState& state, SignalId id, uint32_t now_ms,
                        uint32_t window_ms, float& out) {
  const size_t idx = static_cast<size_t>(id);
  if (idx >= static_cast<size_t>(SignalId::kCount)) return false;
  const auto& lg = state.last_good[idx];
  if (!lg.has_value) return false;
  if ((now_ms - lg.last_ok_ms) > window_ms) return false;
  out = lg.value;
  return true;
}

// last_good[] is a mutable UI-only cache:
// - Written by UI render paths to smooth brief stale windows.
// - Not a synchronization primitive; transient values are acceptable.
// - Safe to update through a const AppState& because only this cache is mutated.
static void UpdateLastGood(const AppState& state, SignalId id, float value,
                           uint32_t now_ms) {
  const size_t idx = static_cast<size_t>(id);
  if (idx >= static_cast<size_t>(SignalId::kCount)) return;
  state.last_good[idx].value = value;
  state.last_good[idx].last_ok_ms = now_ms;
  state.last_good[idx].has_value = true;
}

bool fetch(const DataStore& store, SignalId id, uint32_t now_ms, float& out,
           bool* invalid = nullptr, bool* stale = nullptr) {
  const SignalRead r = store.get(id, now_ms);
  const bool inv = (r.flags & kFlagInvalid) != 0;
  const bool st = (r.flags & kFlagStale) != 0;
  if (invalid) *invalid = inv;
  if (stale) *stale = st;
  if (inv) return false;
  if (!r.valid) return false;
  out = r.value;
  return true;
}

PageRenderData renderOilP(const AppState& state, const ScreenSettings& cfg,
                          const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  const UserSensorCfg cfg_us = state.user_sensor[0];
  const char* label = cfg_us.label[0] ? cfg_us.label : defaultLabel(cfg_us.preset);
  d.label = label;
  const SignalId src = sourceToSignal(cfg_us.source);
  float decoded = 0.0f;
  bool invalid = false;
  bool stale = false;
  if (fetch(store, src, now_ms, decoded, &invalid, &stale)) {
    UpdateLastGood(state, src, decoded, now_ms);
    float canon = 0.0f;
    if (computeCanonical(cfg_us, decoded, canon)) {
      float disp = 0.0f;
      const char* unit = "";
      formatDisplay(cfg_us, cfg.imperial_units, canon, disp, unit);
      formatFloat1(d.big, sizeof(d.big), disp);
      d.unit = unit;
      d.valid = true;
      d.canon_value = canon;
      d.has_canon = true;
    }
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    float cached = 0.0f;
    if (UseLastGood(state, src, now_ms, 2000, cached)) {
      float canon = 0.0f;
      if (computeCanonical(cfg_us, cached, canon)) {
        float disp = 0.0f;
        const char* unit = "";
        formatDisplay(cfg_us, cfg.imperial_units, canon, disp, unit);
        formatFloat1(d.big, sizeof(d.big), disp);
        d.unit = unit;
        d.valid = true;
        d.canon_value = canon;
        d.has_canon = true;
        strlcpy(d.suffix, "!", sizeof(d.suffix));
      }
    } else {
      MarkStale(d);
    }
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderOilT(const AppState& state, const ScreenSettings& cfg,
                          const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  const UserSensorCfg cfg_us = state.user_sensor[1];
  const char* label = cfg_us.label[0] ? cfg_us.label : defaultLabel(cfg_us.preset);
  d.label = label;
  const SignalId src = sourceToSignal(cfg_us.source);
  float decoded = 0.0f;
  bool invalid = false;
  bool stale = false;
  if (fetch(store, src, now_ms, decoded, &invalid, &stale)) {
    UpdateLastGood(state, src, decoded, now_ms);
    float canon = 0.0f;
    if (computeCanonical(cfg_us, decoded, canon)) {
      float disp = 0.0f;
      const char* unit = "";
      formatDisplay(cfg_us, cfg.imperial_units, canon, disp, unit);
      formatFloat1(d.big, sizeof(d.big), disp);
      d.unit = unit;
      d.valid = true;
      d.canon_value = canon;
      d.has_canon = true;
    }
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    float cached = 0.0f;
    if (UseLastGood(state, src, now_ms, 2000, cached)) {
      float canon = 0.0f;
      if (computeCanonical(cfg_us, cached, canon)) {
        float disp = 0.0f;
        const char* unit = "";
        formatDisplay(cfg_us, cfg.imperial_units, canon, disp, unit);
        formatFloat1(d.big, sizeof(d.big), disp);
        d.unit = unit;
        d.valid = true;
        d.canon_value = canon;
        d.has_canon = true;
        strlcpy(d.suffix, "!", sizeof(d.suffix));
      }
    } else {
      MarkStale(d);
    }
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}
PageRenderData renderBoost(const AppState& state, const DataStore& store,
                            const ScreenSettings& cfg, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "BOOST";
  bool inv_map = false;
  bool stale_map = false;
  float map_kpa = 0.0f;
  if (!fetch(store, SignalId::kMap, now_ms, map_kpa, &inv_map, &stale_map)) {
    if (inv_map) {
      MarkInvalid(d);
    } else if (stale_map) {
      float cached = 0.0f;
      if (UseLastGood(state, SignalId::kMap, now_ms, 2000, cached)) {
        map_kpa = cached;
      } else {
        MarkStale(d);
        return d;
      }
    }
  }
  UpdateLastGood(state, SignalId::kMap, map_kpa, now_ms);
  const bool baro_ok = state.baro_acquired;
  const float baro_ref = baro_ok ? state.baro_kpa : 100.0f;
  const float boost_kpa = map_kpa - baro_ref;
  const float boost_psi = boost_kpa * kKpaToPsi;
  const float val = cfg.imperial_units ? boost_psi : boost_kpa;
  const char* unit = cfg.imperial_units ? "psi" : "kPa";
  formatFloat1(d.big, sizeof(d.big), val);
  d.unit = unit;
  if (stale_map) {
    strlcpy(d.suffix, "!", sizeof(d.suffix));
  }
  if (!baro_ok) {
    strlcpy(d.suffix, "(est)", sizeof(d.suffix));
  }
  d.valid = true;
  d.canon_value = boost_kpa;
  d.has_canon = baro_ok;
  return d;
}


PageRenderData renderMap(const AppState& state, const DataStore& store,
                         const ScreenSettings& cfg, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "MAP";
#ifdef DEBUG_STALE_OLED2
  static uint32_t last_map_dbg_ms = 0;
  if ((now_ms - last_map_dbg_ms) >= 1000U) {
    const SignalRead dbg = store.get(SignalId::kMap, now_ms);
    LOGV("[MAPDBG] t=%lu age=%lu flags=0x%02X valid=%d val=%.3f\n",
         static_cast<unsigned long>(now_ms),
         static_cast<unsigned long>(dbg.age_ms), dbg.flags, dbg.valid,
         static_cast<double>(dbg.value));
    (void)dbg;
    last_map_dbg_ms = now_ms;
  }
#endif
  bool invalid = false;
  bool stale = false;
  float map_kpa = 0.0f;
  if (!fetch(store, SignalId::kMap, now_ms, map_kpa, &invalid, &stale)) {
    if (invalid) {
      MarkInvalid(d);
    } else if (stale) {
      float cached = 0.0f;
      if (UseLastGood(state, SignalId::kMap, now_ms, 2000, cached)) {
        const float map_psi = cached * kKpaToPsi;
        const float val = cfg.imperial_units ? map_psi : cached;
        const char* unit = cfg.imperial_units ? "psi" : "kPa";
        formatInt(d.big, sizeof(d.big),
                  static_cast<uint32_t>(val + 0.5f));  // rounded integer
        d.unit = unit;
        d.valid = true;
        d.canon_value = cached;
        d.has_canon = true;
        strlcpy(d.suffix, "!", sizeof(d.suffix));
      } else {
        MarkStale(d);
#ifdef DEBUG_STALE_OLED2
        const SignalRead r = store.get(SignalId::kMap, now_ms);
        LOGV("[STALE] MAP render stale: t=%lu age=%lu flags=0x%02X val=%.3f\n",
             static_cast<unsigned long>(now_ms),
             static_cast<unsigned long>(r.age_ms), r.flags,
             static_cast<double>(r.value));
        (void)r;
#endif
      }
    }
    return d;
  }
  UpdateLastGood(state, SignalId::kMap, map_kpa, now_ms);
  const float map_psi = map_kpa * kKpaToPsi;
  const float val = cfg.imperial_units ? map_psi : map_kpa;
  const char* unit = cfg.imperial_units ? "psi" : "kPa";
  formatInt(d.big, sizeof(d.big),
            static_cast<uint32_t>(val + 0.5f));  // rounded integer
  d.unit = unit;
  d.valid = true;
  d.canon_value = map_kpa;
  d.has_canon = true;
  return d;
}

PageRenderData renderRpm(const AppState& state, const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "RPM";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kRpm, now_ms, v, &invalid, &stale)) {
    formatInt(d.big, sizeof(d.big), static_cast<uint32_t>(v));
    d.unit = "rpm";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderClt(const AppState& state, const ScreenSettings& cfg,
                         const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "CLT";
  bool invalid = false;
  bool stale = false;
  float f = 0.0f;
  if (fetch(store, SignalId::kClt, now_ms, f, &invalid, &stale)) {
    float disp = cfg.imperial_units ? f : f_to_c(f);
    formatInt(d.big, sizeof(d.big), static_cast<uint32_t>(disp));
    d.unit = cfg.imperial_units ? "F" : "C";
    d.valid = true;
    d.canon_value = f;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderMat(const AppState& state, const ScreenSettings& cfg,
                         const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "IAT";
  bool invalid = false;
  bool stale = false;
  float f = 0.0f;
  if (fetch(store, SignalId::kMat, now_ms, f, &invalid, &stale)) {
    float disp = cfg.imperial_units ? f : f_to_c(f);
    formatInt(d.big, sizeof(d.big), static_cast<uint32_t>(disp));
    d.unit = cfg.imperial_units ? "F" : "C";
    d.valid = true;
    d.canon_value = f;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderBatt(const AppState& state, const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "BATT";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kBatt, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "V";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderTps(const AppState& state, const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "TPS";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kTps, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "%";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderAdv(const AppState& state, const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "ADV";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kAdv, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "DEG";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderPw1(const AppState& state, const DataStore& store,
                         uint32_t now_ms) {
  PageRenderData d{};
  d.label = "PW1";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kPw1, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "ms";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderPw2(const AppState& state, const DataStore& store,
                         uint32_t now_ms) {
  PageRenderData d{};
  d.label = "PW2";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kPw2, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "ms";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderPwSeq(const AppState& state, const DataStore& store,
                           uint32_t now_ms) {
  PageRenderData d{};
  d.label = "PWSEQ";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kPwSeq1, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "ms";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderEgo(const AppState& state, const DataStore& store,
                         uint32_t now_ms) {
  PageRenderData d{};
  d.label = "EGO";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kEgoCor1, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "%";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderLaunch(const AppState& state, const DataStore& store,
                            uint32_t now_ms) {
  PageRenderData d{};
  d.label = "LCH";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kLaunchTiming, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "deg";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderTc(const AppState& state, const DataStore& store,
                        uint32_t now_ms) {
  PageRenderData d{};
  d.label = "TC";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kTcRetard, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "deg";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderAfr(const AppState& state, const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = state.afr_show_lambda ? "LAM" : "AFR";
  d.unit = state.afr_show_lambda ? "" : "AFR";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kAfr1, now_ms, v, &invalid, &stale)) {
    float disp = v;
    if (state.afr_show_lambda) {
      const float stoich = (state.stoich_afr < 10.0f) ? 10.0f
                           : (state.stoich_afr > 25.0f) ? 25.0f
                                                        : state.stoich_afr;
      if (stoich > 0.0f) {
        disp = v / stoich;
      }
      snprintf(d.big, sizeof(d.big), "%.2f", static_cast<double>(disp));
    } else {
      formatFloat1(d.big, sizeof(d.big), disp);
    }
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderAfrTgt(const AppState& state, const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = state.afr_show_lambda ? "LAM TG" : "AFR TG";
  d.unit = state.afr_show_lambda ? "" : "AFR";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kAfrTarget1, now_ms, v, &invalid, &stale)) {
    float disp = v;
    if (state.afr_show_lambda) {
      const float stoich = (state.stoich_afr < 10.0f) ? 10.0f
                           : (state.stoich_afr > 25.0f) ? 25.0f
                                                        : state.stoich_afr;
      if (stoich > 0.0f) {
        disp = v / stoich;
      }
      snprintf(d.big, sizeof(d.big), "%.2f", static_cast<double>(disp));
    } else {
      formatFloat1(d.big, sizeof(d.big), disp);
    }
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderKnk(const AppState& state, const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "KNK";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kKnkRetard, now_ms, v, &invalid, &stale)) {
    formatFloat1(d.big, sizeof(d.big), v);
    d.unit = "deg";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderVss(const AppState& state, const ScreenSettings& cfg,
                         const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "VSS";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kVss1, now_ms, v, &invalid, &stale)) {
    const float val = cfg.imperial_units ? v * kMsToMph : v * kMsToKmh;
    formatFloat1(d.big, sizeof(d.big), val);
    d.unit = cfg.imperial_units ? "mph" : "km/h";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

PageRenderData renderEgt(const AppState& state, const ScreenSettings& cfg,
                         const DataStore& store, uint32_t now_ms) {
  PageRenderData d{};
  d.label = "EGT";
  bool invalid = false;
  bool stale = false;
  float v = 0.0f;
  if (fetch(store, SignalId::kEgt1, now_ms, v, &invalid, &stale)) {
    float disp = cfg.imperial_units ? v : f_to_c(v);
    formatInt(d.big, sizeof(d.big), static_cast<uint32_t>(disp));
    d.unit = cfg.imperial_units ? "F" : "C";
    d.valid = true;
    d.canon_value = v;
    d.has_canon = true;
  } else if (invalid) {
    MarkInvalid(d);
  } else if (stale) {
    MarkStale(d);
  } else if (CanLinkLive(state, now_ms)) {
    MarkInvalid(d);
  }
  return d;
}

}  // namespace

float ThresholdStep(ValueKind kind) {
  switch (kind) {
    case ValueKind::kRpm:
      return 100.0f;
    case ValueKind::kSpeed:
      return 1.0f;
    case ValueKind::kTemp:
      return 1.0f;
    case ValueKind::kVoltage:
      return 0.1f;
    case ValueKind::kPercent:
      return 1.0f;
    case ValueKind::kPressure:
      return 1.0f;
    case ValueKind::kDeg:
      return 1.0f;
    case ValueKind::kAfr:
      return 0.1f;
    default:
      return 1.0f;
  }
}

float CanonToDisplay(ValueKind kind, float canon, const ScreenSettings& cfg) {
  switch (kind) {
    case ValueKind::kPressure:
      return cfg.imperial_units ? (canon * kKpaToPsi) : canon;
    case ValueKind::kTemp:
      return cfg.imperial_units ? canon : f_to_c(canon);
    case ValueKind::kVoltage:
    case ValueKind::kAfr:
    case ValueKind::kPercent:
    case ValueKind::kDeg:
      return canon;
    case ValueKind::kRpm:
      return canon;
    case ValueKind::kSpeed:
      return cfg.imperial_units ? canon * kMsToMph : canon * kMsToKmh;
    case ValueKind::kBoost:
      return cfg.imperial_units ? (canon * kKpaToPsi) : canon;
    default:
      return canon;
  }
}

float DisplayToCanon(ValueKind kind, float display, const ScreenSettings& cfg) {
  switch (kind) {
    case ValueKind::kPressure:
      return cfg.imperial_units ? (display / kKpaToPsi) : display;
    case ValueKind::kTemp:
      return cfg.imperial_units ? display : c_to_f(display);
    case ValueKind::kVoltage:
    case ValueKind::kAfr:
    case ValueKind::kPercent:
    case ValueKind::kDeg:
      return display;
    case ValueKind::kRpm:
      return display;
    case ValueKind::kSpeed:
      return cfg.imperial_units ? (display / kMsToMph) : (display / kMsToKmh);
    case ValueKind::kBoost:
      return cfg.imperial_units ? (display / kKpaToPsi) : display;
    default:
      return display;
  }
}

static bool ThresholdGridForUserSensor(const UserSensorCfg& cfg, bool imperial,
                                       ThresholdGrid& out) {
  switch (cfg.kind) {
    case UserSensorKind::kPressure:
      out = imperial ? ThresholdGrid{0.0f, 150.0f, 5.0f, 0}
                     : ThresholdGrid{0.0f, 1000.0f, 50.0f, 0};
      return true;
    case UserSensorKind::kTemp:
      out = imperial ? ThresholdGrid{120.0f, 320.0f, 10.0f, 0}
                     : ThresholdGrid{50.0f, 160.0f, 5.0f, 0};
      return true;
    case UserSensorKind::kVoltage:
      out = {0.0f, 5.0f, 0.1f, 1};
      return true;
    case UserSensorKind::kUnitless:
    default:
      out = {-100.0f, 100.0f, 1.0f, 0};
      return true;
  }
}

bool GetThresholdGrid(PageId id, bool imperial, ThresholdGrid& out) {
  switch (id) {
    case PageId::kBatt:
      out = {9.0f, 20.0f, 0.5f, 1};
      return true;
    case PageId::kRpm:
      out = {0.0f, 9000.0f, 250.0f, 0};
      return true;
    case PageId::kTps:
      out = {0.0f, 100.0f, 5.0f, 0};
      return true;
    case PageId::kAdv:
      out = {-20.0f, 60.0f, 1.0f, 0};
      return true;
    case PageId::kKnk:
      out = {0.0f, 20.0f, 1.0f, 0};
      return true;
    case PageId::kAfr1:
    case PageId::kAfrTgt:
      out = {8.0f, 25.0f, 0.5f, 1};
      return true;
    case PageId::kVss:
      out = imperial ? ThresholdGrid{0.0f, 200.0f, 10.0f, 0}
                     : ThresholdGrid{0.0f, 300.0f, 10.0f, 0};
      return true;
    case PageId::kClt:
      out = imperial ? ThresholdGrid{140.0f, 270.0f, 10.0f, 0}
                     : ThresholdGrid{60.0f, 130.0f, 5.0f, 0};
      return true;
    case PageId::kMat:
      out = imperial ? ThresholdGrid{-4.0f, 300.0f, 10.0f, 0}
                     : ThresholdGrid{-20.0f, 150.0f, 5.0f, 0};
      return true;
    case PageId::kOilT:
      out = imperial ? ThresholdGrid{120.0f, 320.0f, 10.0f, 0}
                     : ThresholdGrid{50.0f, 160.0f, 5.0f, 0};
      return true;
    case PageId::kEgt1:
      out = imperial ? ThresholdGrid{400.0f, 2000.0f, 50.0f, 0}
                     : ThresholdGrid{200.0f, 1100.0f, 25.0f, 0};
      return true;
    case PageId::kOilP:
      out = imperial ? ThresholdGrid{0.0f, 150.0f, 5.0f, 0}
                     : ThresholdGrid{0.0f, 1000.0f, 50.0f, 0};
      return true;
    case PageId::kMapAbs:
      out = imperial ? ThresholdGrid{0.0f, 45.0f, 1.0f, 0}
                     : ThresholdGrid{0.0f, 300.0f, 10.0f, 0};
      return true;
    case PageId::kBoost:
      out = imperial ? ThresholdGrid{-15.0f, 45.0f, 1.0f, 0}
                     : ThresholdGrid{-100.0f, 300.0f, 10.0f, 0};
      return true;
    default:
      return false;
  }
}

bool GetThresholdGridWithState(PageId id, const AppState& state, bool imperial,
                               ThresholdGrid& out) {
  if (id == PageId::kOilP) {
    return ThresholdGridForUserSensor(state.user_sensor[0], imperial, out);
  }
  if (id == PageId::kOilT) {
    return ThresholdGridForUserSensor(state.user_sensor[1], imperial, out);
  }
  return GetThresholdGrid(id, imperial, out);
}

float SnapToGrid(float disp, const ThresholdGrid& g) {
  float v = disp;
  if (v < g.min_disp) v = g.min_disp;
  if (v > g.max_disp) v = g.max_disp;
  if (g.step_disp > 0.0f) {
    const float steps = roundf((v - g.min_disp) / g.step_disp);
    v = g.min_disp + steps * g.step_disp;
  }
  if (v < g.min_disp) v = g.min_disp;
  if (v > g.max_disp) v = g.max_disp;
  return v;
}

bool IsOnGrid(float disp, const ThresholdGrid& g) {
  const float snapped = SnapToGrid(disp, g);
  const float eps = (g.step_disp > 0.0f) ? (g.step_disp * 0.001f) : 0.001f;
  return fabsf(disp - snapped) < eps;
}

PageRenderData BuildPageData(PageId id, const AppState& state,
                             const ScreenSettings& cfg, const DataStore& store,
                             uint32_t now_ms) {
  switch (id) {
    case PageId::kOilP:
      return renderOilP(state, cfg, store, now_ms);
    case PageId::kOilT:
      return renderOilT(state, cfg, store, now_ms);
    case PageId::kBoost:
      return renderBoost(state, store, cfg, now_ms);
    case PageId::kMapAbs:
      return renderMap(state, store, cfg, now_ms);
    case PageId::kRpm:
      return renderRpm(state, store, now_ms);
    case PageId::kClt:
      return renderClt(state, cfg, store, now_ms);
    case PageId::kMat:
      return renderMat(state, cfg, store, now_ms);
    case PageId::kBatt:
      return renderBatt(state, store, now_ms);
    case PageId::kTps:
      return renderTps(state, store, now_ms);
    case PageId::kAdv:
      return renderAdv(state, store, now_ms);
    case PageId::kAfr1:
      return renderAfr(state, store, now_ms);
    case PageId::kAfrTgt:
      return renderAfrTgt(state, store, now_ms);
    case PageId::kKnk:
      return renderKnk(state, store, now_ms);
    case PageId::kVss:
      return renderVss(state, cfg, store, now_ms);
    case PageId::kEgt1:
      return renderEgt(state, cfg, store, now_ms);
    case PageId::kPw1:
      return renderPw1(state, store, now_ms);
    case PageId::kPw2:
      return renderPw2(state, store, now_ms);
    case PageId::kPwSeq:
      return renderPwSeq(state, store, now_ms);
    case PageId::kEgo:
      return renderEgo(state, store, now_ms);
    case PageId::kLaunch:
      return renderLaunch(state, store, now_ms);
    case PageId::kTc:
      return renderTc(state, store, now_ms);
    default:
      return {};
  }
}

bool PageCanonicalValue(PageId id, const AppState& state,
                        const ScreenSettings& cfg, const DataStore& store,
                        uint32_t now_ms, float& out) {
  switch (id) {
    case PageId::kOilP: {
      const UserSensorCfg cfg_us = state.user_sensor[0];
      const SignalId src = sourceToSignal(cfg_us.source);
      float decoded = 0.0f;
      if (!fetch(store, src, now_ms, decoded)) return false;
      float canon = 0.0f;
      if (!computeCanonical(cfg_us, decoded, canon)) return false;
      out = canon;
      return true;
    }
    case PageId::kOilT: {
      const UserSensorCfg cfg_us = state.user_sensor[1];
      const SignalId src = sourceToSignal(cfg_us.source);
      float decoded = 0.0f;
      if (!fetch(store, src, now_ms, decoded)) return false;
      float canon = 0.0f;
      if (!computeCanonical(cfg_us, decoded, canon)) return false;
      out = canon;
      return true;
    }
    case PageId::kBoost: {
      if (!state.baro_acquired) return false;
      float map = 0.0f;
      if (!fetch(store, SignalId::kMap, now_ms, map)) return false;
      out = map - state.baro_kpa;
      return true;
    }
    case PageId::kMapAbs:
      return fetch(store, SignalId::kMap, now_ms, out);
    case PageId::kRpm:
      return fetch(store, SignalId::kRpm, now_ms, out);
    case PageId::kClt:
      return fetch(store, SignalId::kClt, now_ms, out);
    case PageId::kMat:
      return fetch(store, SignalId::kMat, now_ms, out);
    case PageId::kBatt:
      return fetch(store, SignalId::kBatt, now_ms, out);
    case PageId::kTps:
      return fetch(store, SignalId::kTps, now_ms, out);
    case PageId::kAdv:
      return fetch(store, SignalId::kAdv, now_ms, out);
    case PageId::kAfr1:
      return fetch(store, SignalId::kAfr1, now_ms, out);
    case PageId::kAfrTgt:
      return fetch(store, SignalId::kAfrTarget1, now_ms, out);
    case PageId::kKnk:
      return fetch(store, SignalId::kKnkRetard, now_ms, out);
    case PageId::kVss:
      return fetch(store, SignalId::kVss1, now_ms, out);
    case PageId::kEgt1:
      return fetch(store, SignalId::kEgt1, now_ms, out);
    case PageId::kPw1:
      return fetch(store, SignalId::kPw1, now_ms, out);
    case PageId::kPw2:
      return fetch(store, SignalId::kPw2, now_ms, out);
    case PageId::kPwSeq:
      return fetch(store, SignalId::kPwSeq1, now_ms, out);
    case PageId::kEgo:
      return fetch(store, SignalId::kEgoCor1, now_ms, out);
    case PageId::kLaunch:
      return fetch(store, SignalId::kLaunchTiming, now_ms, out);
    case PageId::kTc:
      return fetch(store, SignalId::kTcRetard, now_ms, out);
    default:
      return false;
  }
}
