#pragma once

#include <Arduino.h>

#include "ecu/ecu_profile.h"
#include "ms3_decode/ms3_decode.h"

class Ms3EvoPlusProfile : public IEcuProfile {
 public:
  Ms3EvoPlusProfile();

  const char* name() const override;
  bool acceptFrame(const twai_message_t& msg) const override;
  bool acceptId(uint32_t id) const override;
  bool decode(const twai_message_t& msg, DecodedSignal* out,
              uint8_t& count) const override;

  const DashSpec& dashSpec() const override;
  int dashIndexForId(uint32_t id) const override;
  uint8_t dashIdCount() const override;
  uint32_t dashIdAt(uint8_t i) const override;

  const uint32_t* scanBitrates(uint8_t& count) const override;
  bool hasFixedBitrate() const override;
  uint32_t fixedBitrate() const override;
  uint32_t preferredBitrate() const override;
  bool requiresAckPeer() const override { return true; }
  const ValidationSpec& validationSpec() const override;
  SignalSpan dashSignalsForIndex(uint8_t idx) const override;
  const AutobaudSpec& autobaudSpec() const override;

 private:
  Ms3Decoder decoder_;
  DashSpec dash_spec_;
  ValidationSpec validation_spec_;
  static constexpr uint8_t kDashCount = 5;
  static constexpr uint32_t kDashIds[kDashCount] = {0x5E8, 0x5E9, 0x5EA, 0x5EB,
                                                    0x5EC};
  static constexpr uint32_t kScanRates[4] = {500000, 250000, 1000000, 125000};
  SignalSpan signal_map_[kDashCount];
  AutobaudSpec autobaud_spec_;
};
