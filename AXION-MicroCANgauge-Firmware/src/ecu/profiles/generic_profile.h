#pragma once

#include "ecu/ecu_profile.h"

// Minimal placeholder profile for future ECUs. It accepts no frames and decodes
// nothing, serving as a safe “does nothing” default when selected.
class GenericProfile : public IEcuProfile {
 public:
  static GenericProfile& instance();

  const char* name() const override { return "Generic"; }

  bool acceptFrame(const twai_message_t& msg) const override;
  bool acceptId(uint32_t id) const override;

  bool decode(const twai_message_t& msg, DecodedSignal* out,
              uint8_t& count) const override;

  const DashSpec& dashSpec() const override { return dash_; }
  int dashIndexForId(uint32_t) const override { return -1; }
  uint8_t dashIdCount() const override { return 0; }
  uint32_t dashIdAt(uint8_t) const override { return 0; }
  const ValidationSpec& validationSpec() const override { return validation_; }
  SignalSpan dashSignalsForIndex(uint8_t) const override { return {}; }
  const AutobaudSpec& autobaudSpec() const override { return autobaud_; }

  const uint32_t* scanBitrates(uint8_t& count) const override;
  bool hasFixedBitrate() const override { return false; }
  uint32_t fixedBitrate() const override { return 0; }
  uint32_t preferredBitrate() const override;
 bool requiresAckPeer() const override { return false; }

 private:
  // Singleton only.
  GenericProfile();

  DashSpec dash_;
  ValidationSpec validation_;
  AutobaudSpec autobaud_;
};
