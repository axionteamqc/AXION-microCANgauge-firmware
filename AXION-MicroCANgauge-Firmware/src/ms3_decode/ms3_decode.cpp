#include "ms3_decode/ms3_decode.h"

#include <cmath>
#include <Arduino.h>
#if ARDUINO_USB_CDC_ON_BOOT && !defined(CONFIG_TINYUSB_CDC_ENABLED)
#define Serial Serial0
#endif

#include "config/logging.h"
#include "ecu/bit_extract.h"
#include "ecu/bit_order.h"

bool Ms3Decoder::decode(const twai_message_t& msg, Ms3SignalValue* out,
                        uint8_t& count) const {
  count = 0;
  if (msg.extd || msg.rtr || msg.data_length_code == 0) {
    return false;
  }
  const uint32_t id = msg.identifier;
  const Ms3MessageSpec* spec = nullptr;
  for (size_t i = 0; i < kMs3MessageCount; ++i) {
    if (kMs3Messages[i].can_id == id) {
      spec = &kMs3Messages[i];
      break;
    }
  }
  if (!spec) {
    return false;
  }
  for (uint8_t i = 0; i < spec->signal_count; ++i) {
    const Ms3SignalSpec& sig = spec->signals[i];
    const uint64_t raw_u =
        extractBits(msg.data, sig.start_bit, sig.length, sig.bit_order);
    int64_t raw = static_cast<int64_t>(raw_u);
    if (sig.is_signed && sig.length > 0) {
      const int64_t sign_mask = 1LL << (sig.length - 1);
      if (raw & sign_mask) {
        const uint64_t extend_mask_u = (~0ULL) << sig.length;
        raw |= static_cast<int64_t>(extend_mask_u);
      }
    }
    const float phys = static_cast<float>(raw) * sig.scale + sig.offset;
    out[count++] = Ms3SignalValue{sig.id, phys};
  }
  return count > 0;
}

bool RunMs3DecodeGoldenTest(const Ms3Decoder& decoder) {
  twai_message_t msg{};
  msg.identifier = 0x5E8;
  msg.data_length_code = 8;
  msg.extd = 0;
  msg.rtr = 0;
  // Raw chosen to match table: MAP start 7|16 BE, RPM 23|16 u16, CLT 39|16 s16,
  // TPS 55|16 s16 (per table ordering).
  msg.data[0] = 0x27;  // MAP = 0x27 0x10 -> 10000 raw -> 1000.0 kPa after scale 0.1
  msg.data[1] = 0x10;
  msg.data[2] = 0x13;  // RPM = 0x13 0x88 -> 5000 rpm
  msg.data[3] = 0x88;
  msg.data[4] = 0x00;  // CLT = 0x00 0xC8 -> 200 raw -> 20.0 before offset (scale 0.1)
  msg.data[5] = 0xC8;
  msg.data[6] = 0x00;  // TPS = 0x00 0x64 -> 100 raw -> 10.0% (scale 0.1)
  msg.data[7] = 0x64;

  Ms3SignalValue decoded[8];
  uint8_t count = 0;
  const bool ok = decoder.decode(msg, decoded, count);
  bool pass = ok && (count == 4);
  if (pass) {
    // Expected in physical units per ms3_decode_table
    constexpr float kExpected[] = {1000.0f, 5000.0f, 20.0f, 10.0f};
    for (uint8_t i = 0; i < count && pass; ++i) {
      const float diff = fabsf(decoded[i].phys - kExpected[i]);
      if (diff > 0.05f) {
        pass = false;
      }
    }
  }

  LOGI("%s\r\n", pass ? "DECODE_GOLDEN_OK" : "DECODE_GOLDEN_FAIL");
  return pass;
}
