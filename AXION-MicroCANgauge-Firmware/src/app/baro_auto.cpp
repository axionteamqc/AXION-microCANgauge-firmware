#include "app/baro_auto.h"

#include "app/app_globals.h"
#include "app_config.h"
#include "config/logging.h"

namespace {

struct BaroAutoContext {
  uint32_t window_start_ms = 0;
  float sum_kpa = 0.0f;
  uint32_t count = 0;
  float min_kpa = 0.0f;
  float max_kpa = 0.0f;
};

BaroAutoContext g_baro_auto;

}  // namespace

void ResetBaroAuto() {
  g_baro_auto.window_start_ms = 0;
  g_baro_auto.sum_kpa = 0.0f;
  g_baro_auto.count = 0;
  g_baro_auto.min_kpa = 0.0f;
  g_baro_auto.max_kpa = 0.0f;
}

void AutoAcquireBaro(AppState& state, uint32_t now_ms) {
  const float kRpmThreshold = 200.0f;
  const uint32_t kMaxAgeMs = 800;
  const uint32_t kWindowMs = 1200;
  const uint32_t kMinSamples = 8;
  const float kStableDeltaKpa = 1.5f;

  if (state.baro_acquired || state.demo_mode || !AppConfig::IsRealCanEnabled() ||
      !state.can_ready) {
    ResetBaroAuto();
    return;
  }

  const DataStore& store = ActiveStore();
  const SignalRead rpm = store.get(SignalId::kRpm, now_ms);
  if (!rpm.valid || rpm.age_ms > kMaxAgeMs || rpm.value >= kRpmThreshold) {
    ResetBaroAuto();
    return;
  }
  const SignalRead map = store.get(SignalId::kMap, now_ms);
  if (!map.valid || map.age_ms > kMaxAgeMs) {
    ResetBaroAuto();
    return;
  }

  const float map_kpa = map.value;
  if (g_baro_auto.window_start_ms == 0) {
    g_baro_auto.window_start_ms = now_ms;
    g_baro_auto.sum_kpa = 0.0f;
    g_baro_auto.count = 0;
    g_baro_auto.min_kpa = map_kpa;
    g_baro_auto.max_kpa = map_kpa;
  }

  float new_min = (map_kpa < g_baro_auto.min_kpa) ? map_kpa : g_baro_auto.min_kpa;
  float new_max = (map_kpa > g_baro_auto.max_kpa) ? map_kpa : g_baro_auto.max_kpa;
  if (g_baro_auto.count > 0 && (new_max - new_min) > kStableDeltaKpa) {
    g_baro_auto.window_start_ms = now_ms;
    g_baro_auto.sum_kpa = map_kpa;
    g_baro_auto.count = 1;
    g_baro_auto.min_kpa = map_kpa;
    g_baro_auto.max_kpa = map_kpa;
  } else {
    g_baro_auto.sum_kpa += map_kpa;
    ++g_baro_auto.count;
    g_baro_auto.min_kpa = new_min;
    g_baro_auto.max_kpa = new_max;
  }

  if (g_baro_auto.count >= kMinSamples &&
      (now_ms - g_baro_auto.window_start_ms) >= kWindowMs &&
      (g_baro_auto.max_kpa - g_baro_auto.min_kpa) <= kStableDeltaKpa) {
    const float avg = g_baro_auto.sum_kpa / static_cast<float>(g_baro_auto.count);
    state.baro_kpa = avg;
    state.baro_acquired = true;
    SetupPersist persist{};
    g_nvs.loadSetupPersist(persist);
    persist.baro_acquired = true;
    persist.baro_kpa = avg;
    g_nvs.saveSetupPersist(persist);
#if BARO_DEBUG
    LOGI("BARO auto-acquired: %.2f kPa\r\n", static_cast<double>(avg));
#endif
    for (uint8_t z = 0; z < kMaxZones; ++z) {
      state.force_redraw[z] = true;
    }
    ResetBaroAuto();
  }
}
