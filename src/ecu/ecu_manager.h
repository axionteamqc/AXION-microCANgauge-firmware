#pragma once

#include <Arduino.h>

#include "ecu/ecu_profile.h"
#include "ecu/profiles/generic_profile.h"
#include "ecu/profiles/ms3_evoplus_profile.h"

class TwaiLink;

enum class EcuProfileId : uint8_t {
  kAuto = 0,
  kMs3EvoPlus = 1,
  kGeneric = 2,
};

class EcuManager {
 public:
  EcuManager() : active_(nullptr) {}

  void initForcedMs3();  // release: force Megasquirt
  bool initFromEcuType(const char* ecu_type);
  const IEcuProfile* detectProfile(uint8_t id_mask);
#ifdef ECU_DETECT_DEBUG
  bool detectOnBus(TwaiLink& link, uint32_t window_ms);
#endif

  const IEcuProfile& profile() const { return *active_; }
  const IEcuProfile& activeProfile() const { return *active_; }
  const char* activeName() const { return active_ ? active_->name() : ""; }

 private:
  const IEcuProfile* active_;
};
