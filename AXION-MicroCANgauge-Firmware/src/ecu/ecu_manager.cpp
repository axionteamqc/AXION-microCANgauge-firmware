#include "can_link/twai_link.h"
#include "ecu/ecu_manager.h"
#include "config/factory_config.h"
#include "config/logging.h"

namespace {

Ms3EvoPlusProfile& ms3Profile() {
  static Ms3EvoPlusProfile instance;
  return instance;
}

GenericProfile& genericProfile() { return GenericProfile::instance(); }

}  // namespace

void EcuManager::initForcedMs3() {
  (void)kEcuTarget;
  active_ = &ms3Profile();
}

bool EcuManager::initFromEcuType(const char* ecu_type) {
  if (!ecu_type || ecu_type[0] == '\0') {
    return false;
  }
  if (strcasecmp(ecu_type, "MEGASQUIRT") == 0 ||
      strncasecmp(ecu_type, "MS3", 3) == 0) {
    active_ = &ms3Profile();
    return true;
  }
  if (strcasecmp(ecu_type, "GENERIC") == 0) {
    active_ = &genericProfile();
    return true;
  }
  // Unknown ECU type.
  return false;
}

const IEcuProfile* EcuManager::detectProfile(uint8_t id_mask) {
  // Minimal detection: if MS3 dash base ID appears, select MS3 profile.
  // Future: add additional profiles/DBC matching here.
  Ms3EvoPlusProfile& profile = ms3Profile();
  const DashSpec& dash = profile.dashSpec();
  const bool has_base = (dash.count > 0) && ((id_mask & 0x01U) != 0);
  const bool has_any = (id_mask != 0);
  if (has_base && has_any) {
    active_ = &profile;
    return active_;
  }
  return nullptr;
}

#ifdef ECU_DETECT_DEBUG
bool EcuManager::detectOnBus(TwaiLink& link, uint32_t window_ms) {
  Ms3EvoPlusProfile& profile = ms3Profile();
  uint8_t id_present_mask = 0;
  uint32_t rx_dash = 0;
  const uint32_t start = millis();
  while ((millis() - start) < window_ms) {
    twai_message_t msg{};
    while (link.receive(msg, 0)) {
      if (!profile.acceptFrame(msg)) {
        continue;
      }
      const int idx = profile.dashIndexForId(msg.identifier);
      if (idx >= 0 && idx < 5) {
        rx_dash++;
        id_present_mask |= static_cast<uint8_t>(1U << idx);
      }
    }
    vTaskDelay(1);
  }
  const bool has_base = (id_present_mask & 0x01) != 0;
  const bool has_other = (id_present_mask & 0b11110) != 0;
  const bool detected = has_base && has_other && (rx_dash >= 2);
  if (detected) {
    active_ = &profile;
    LOGI("ECU detect: Megasquirt\r\n");
  } else {
    LOGI("ECU detect: Broadcast not detected / Ask your tuner\r\n");
  }
  return detected;
}
#endif
