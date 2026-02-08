#include "ecu/profiles/generic_profile.h"

namespace {

constexpr uint32_t kGenericBitrates[] = {500000, 250000, 125000, 1000000};

}  // namespace

GenericProfile& GenericProfile::instance() {
  static GenericProfile inst;
  return inst;
}

GenericProfile::GenericProfile() {
  dash_.ids = nullptr;
  dash_.count = 0;
  dash_.require_min_rx_dash = 0;
  dash_.required_first_id = 0;

  validation_.window_ms = 300;
  validation_.min_rx_dash = 1;

  autobaud_.mode = AutobaudMode::kGeneric;
  autobaud_.bitrates = kGenericBitrates;
  autobaud_.bitrate_count =
      static_cast<uint8_t>(sizeof(kGenericBitrates) / sizeof(kGenericBitrates[0]));
  autobaud_.listen_window_ms = 300;
  autobaud_.confirm_window_ms = 300;
  autobaud_.confirm_windows = 1;
  autobaud_.min_rx_total = 1;
  autobaud_.min_rx_dash = 0;
  autobaud_.confirm_min_frames = 1;
  autobaud_.require_bus_off_clear = true;
}

bool GenericProfile::acceptFrame(const twai_message_t& msg) const {
  return !msg.extd && !msg.rtr;
}

bool GenericProfile::acceptId(uint32_t) const { return true; }

bool GenericProfile::decode(const twai_message_t&, DecodedSignal* out,
                            uint8_t& count) const {
  count = 0;
  (void)out;
  return false;
}

const uint32_t* GenericProfile::scanBitrates(uint8_t& count) const {
  count = autobaud_.bitrate_count;
  return autobaud_.bitrates;
}

uint32_t GenericProfile::preferredBitrate() const {
  uint8_t c = 0;
  const uint32_t* rates = scanBitrates(c);
  return (c > 0) ? rates[0] : 0;
}
