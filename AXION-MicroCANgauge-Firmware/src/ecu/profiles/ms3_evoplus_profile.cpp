#include "ecu/profiles/ms3_evoplus_profile.h"

Ms3EvoPlusProfile::Ms3EvoPlusProfile() {
  dash_spec_.ids = kDashIds;
  dash_spec_.count = kDashCount;
  dash_spec_.require_min_rx_dash = 2;
  dash_spec_.required_first_id = 0x5E8;
  validation_spec_.window_ms = 400;
  validation_spec_.min_rx_dash = 12;
  static const SignalId sig0[] = {SignalId::kMap, SignalId::kClt, SignalId::kRpm,
                                  SignalId::kTps};
  static const SignalId sig1[] = {SignalId::kAdv, SignalId::kMat, SignalId::kPw2,
                                  SignalId::kPw1};
  static const SignalId sig2[] = {SignalId::kPwSeq1, SignalId::kEgt1,
                                  SignalId::kEgoCor1, SignalId::kAfr1,
                                  SignalId::kAfrTarget1};
  static const SignalId sig3[] = {SignalId::kKnkRetard, SignalId::kSensors2,
                                  SignalId::kSensors1, SignalId::kBatt};
  static const SignalId sig4[] = {SignalId::kLaunchTiming, SignalId::kTcRetard,
                                  SignalId::kVss1};
  signal_map_[0].ids = sig0;
  signal_map_[0].count = static_cast<uint8_t>(sizeof(sig0) / sizeof(sig0[0]));
  signal_map_[1].ids = sig1;
  signal_map_[1].count = static_cast<uint8_t>(sizeof(sig1) / sizeof(sig1[0]));
  signal_map_[2].ids = sig2;
  signal_map_[2].count = static_cast<uint8_t>(sizeof(sig2) / sizeof(sig2[0]));
  signal_map_[3].ids = sig3;
  signal_map_[3].count = static_cast<uint8_t>(sizeof(sig3) / sizeof(sig3[0]));
  signal_map_[4].ids = sig4;
  signal_map_[4].count = static_cast<uint8_t>(sizeof(sig4) / sizeof(sig4[0]));

  autobaud_spec_.bitrates = kScanRates;
  autobaud_spec_.bitrate_count =
      static_cast<uint8_t>(sizeof(kScanRates) / sizeof(kScanRates[0]));
  autobaud_spec_.mode = AutobaudMode::kProfileSpecific;
  autobaud_spec_.allow_listen_only = false;
  autobaud_spec_.normal_probe_window_ms = 80;
  autobaud_spec_.listen_window_ms = 300;
  autobaud_spec_.confirm_window_ms = 300;
  autobaud_spec_.confirm_windows = 2;
  autobaud_spec_.min_rx_total = 20;
  autobaud_spec_.min_rx_dash = 8;
  autobaud_spec_.confirm_min_frames = 12;
  autobaud_spec_.confirm_min_distinct_ids = 2;
  autobaud_spec_.confirm_min_expected_hits = 8;
  autobaud_spec_.require_min_per_id = 3;
  autobaud_spec_.require_distinct_ids = 4;
  autobaud_spec_.max_rx_missed = 2;
  autobaud_spec_.require_bus_off_clear = true;
}

const char* Ms3EvoPlusProfile::name() const { return "Megasquirt"; }

bool Ms3EvoPlusProfile::acceptFrame(const twai_message_t& msg) const {
  if (msg.extd || msg.rtr) return false;
  return acceptId(msg.identifier);
}

bool Ms3EvoPlusProfile::acceptId(uint32_t id) const {
  return id >= 0x5E8 && id <= 0x5EC;
}

bool Ms3EvoPlusProfile::decode(const twai_message_t& msg, DecodedSignal* out,
                               uint8_t& count) const {
  if (!out) return false;
  if (!acceptFrame(msg)) return false;
  return decoder_.decode(msg, out, count);
}

const DashSpec& Ms3EvoPlusProfile::dashSpec() const { return dash_spec_; }

int Ms3EvoPlusProfile::dashIndexForId(uint32_t id) const {
  if (!acceptId(id)) return -1;
  return static_cast<int>(id - 0x5E8);
}

uint8_t Ms3EvoPlusProfile::dashIdCount() const { return kDashCount; }

uint32_t Ms3EvoPlusProfile::dashIdAt(uint8_t i) const {
  return (i < kDashCount) ? kDashIds[i] : 0;
}

const uint32_t* Ms3EvoPlusProfile::scanBitrates(uint8_t& count) const {
  count = sizeof(kScanRates) / sizeof(kScanRates[0]);
  return kScanRates;
}

bool Ms3EvoPlusProfile::hasFixedBitrate() const { return false; }

uint32_t Ms3EvoPlusProfile::fixedBitrate() const { return 0; }

uint32_t Ms3EvoPlusProfile::preferredBitrate() const {
  uint8_t cnt = 0;
  const uint32_t* rates = scanBitrates(cnt);
  return (cnt > 0 && rates) ? rates[0] : 0;
}

const ValidationSpec& Ms3EvoPlusProfile::validationSpec() const {
  return validation_spec_;
}

SignalSpan Ms3EvoPlusProfile::dashSignalsForIndex(uint8_t idx) const {
  if (idx < kDashCount) {
    return signal_map_[idx];
  }
  SignalSpan empty{};
  return empty;
}

const AutobaudSpec& Ms3EvoPlusProfile::autobaudSpec() const {
  return autobaud_spec_;
}
constexpr uint32_t Ms3EvoPlusProfile::kDashIds[kDashCount];
constexpr uint32_t Ms3EvoPlusProfile::kScanRates[4];
