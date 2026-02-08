#include "app/persist_runtime.h"
// UI persistence debounce and NVS save.

#include "app/app_globals.h"
#include "app_state.h"
#include "settings/nvs_store.h"
#include "settings/ui_persist_build.h"

// Audit note:
// All UI-initiated persistence now goes through deferred requests here.
// No direct saveUiPersist/saveOilPersist is issued in the button/menu path.
// Counters: g_state.persist_audit.{ui_writes,ui_write_max_ms,oil_writes,oil_write_max_ms}
// Button events counted in InputRuntimeTick for correlation with NVS commits.

namespace {
bool g_ui_dirty = false;
uint32_t g_ui_dirty_since = 0;
bool g_oil_dirty = false;
OilPersist g_oil_pending{};
uint32_t g_last_commit_ms = 0;
constexpr uint32_t kUiIdleBeforeSaveMs = 1200;
constexpr uint32_t kMinCommitIntervalMs = 2000;
}  // namespace

void markUiDirty(uint32_t now_ms) {
  g_ui_dirty = true;
  g_ui_dirty_since = now_ms;
}

void PersistRequestUiSave() { markUiDirty(millis()); }

void PersistRequestOilSave(const OilPersist& op) {
  g_oil_dirty = true;
  g_oil_pending = op;
}

void PersistRuntimeTick(uint32_t now_ms) {
  const uint32_t since_commit = g_last_commit_ms == 0 ? 0xFFFFFFFFu : (now_ms - g_last_commit_ms);
  const bool allow_commit = since_commit >= kMinCommitIntervalMs;
  uint32_t last_input_ms = 0;
  portENTER_CRITICAL(&g_state_mux);
  last_input_ms = g_state.last_input_ms;
  portEXIT_CRITICAL(&g_state_mux);
  const bool idle_ui = (now_ms - last_input_ms) >= kUiIdleBeforeSaveMs;
  if (!allow_commit || !idle_ui) return;

  if (g_ui_dirty) {
    UiPersist ui = BuildUiPersistFromState(g_state);
    const uint32_t t0 = now_ms;
    g_nvs.saveUiPersist(ui);
    const uint32_t dt = millis() - t0;
    ++g_state.persist_audit.ui_writes;
    if (dt > g_state.persist_audit.ui_write_max_ms) {
      g_state.persist_audit.ui_write_max_ms = dt;
    }
    g_ui_dirty = false;
    g_last_commit_ms = millis();
    return;
  }

  if (g_oil_dirty) {
    const uint32_t t0 = now_ms;
    g_nvs.saveOilPersist(g_oil_pending);
    const uint32_t dt = millis() - t0;
    ++g_state.persist_audit.oil_writes;
    if (dt > g_state.persist_audit.oil_write_max_ms) {
      g_state.persist_audit.oil_write_max_ms = dt;
    }
    g_oil_dirty = false;
    g_last_commit_ms = millis();
  }
}
