#include "app_config.h"
#if SETUP_WIZARD_ENABLED
// SetupWizard CAN handling: TWAI drain and persistence.
#include "setup_wizard/setup_wizard.h"

#include <algorithm>
#include <cstring>

#include "app/app_globals.h"
#include "ecu/ecu_manager.h"

extern EcuManager g_ecu_mgr;

void SetupWizard::drainCan(AppState& state, uint32_t now_ms) {
  twai_message_t msg{};
  DecodedSignal decoded[8];
  const IEcuProfile& profile = g_ecu_mgr.profile();
  const uint8_t dash_count =
      std::min<uint8_t>(profile.dashIdCount(), static_cast<uint8_t>(5));
  while (twai_.receive(msg, 0)) {
    recordDebug(msg, now_ms);
    ++state.can_stats.rx_total;
    const int idx = profile.dashIndexForId(msg.identifier);
    if (idx >= 0 && static_cast<uint8_t>(idx) < dash_count) {
      uint8_t count = 0;
      if (profile.decode(msg, decoded, count)) {
        ++state.can_stats.rx_dash;
        state.id_present_mask |= static_cast<uint8_t>(1U << idx);
        recordInterval(static_cast<uint8_t>(idx), now_ms);
        for (uint8_t i = 0; i < count; ++i) {
          store_.update(decoded[i].id, decoded[i].phys, now_ms);
          switch (decoded[i].id) {
            case SignalId::kMap:
              last_map_kpa_ = decoded[i].phys;
              last_map_ms_ = now_ms;
              break;
            case SignalId::kRpm:
              last_rpm_ = decoded[i].phys;
              last_rpm_ms_ = now_ms;
              break;
            case SignalId::kTps:
              last_tps_ = decoded[i].phys;
              last_tps_ms_ = now_ms;
              break;
            default:
              break;
          }
        }
      }
    }
  }

  uint32_t alerts = 0;
  while (twai_.readAlerts(alerts, 0)) {
    if (alerts & TWAI_ALERT_BUS_OFF) {
      ++state.can_stats.bus_off;
    }
    if (alerts & TWAI_ALERT_ERR_PASS) {
      ++state.can_stats.err_passive;
    }
    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
      ++state.can_stats.rx_overrun;
    }
  }
  twai_status_info_t status{};
  if (twai_get_status_info(&status) == ESP_OK) {
    state.can_stats.rx_missed = status.rx_missed_count;
  }
}

void SetupWizard::saveProgressKoeo(AppState& state, bool mark_done) {
  SetupPersist persist{};
  nvs_.loadSetupPersist(persist);
  if (mark_done) {
    persist.phase = static_cast<uint8_t>(SetupPersist::Phase::kKoeoDone);
  }
  persist.baro_acquired = baro_acquired_;
  persist.baro_kpa = baro_kpa_;
  for (uint8_t i = 0; i < 5; ++i) {
    persist.stale_ms[i] = stale_ms_[i];
  }
  nvs_.saveSetupPersist(persist);
}

void SetupWizard::saveProgressRun(AppState& state) {
  SetupPersist persist{};
  nvs_.loadSetupPersist(persist);
  persist.phase = static_cast<uint8_t>(SetupPersist::Phase::kRunDone);
  nvs_.saveSetupPersist(persist);
  state.wizard_active = false;
}

#endif  // SETUP_WIZARD_ENABLED
