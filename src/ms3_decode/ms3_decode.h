#pragma once

#include <Arduino.h>
#include <driver/twai.h>

#include "data/datastore.h"
#include "ms3_decode/ms3_decode_table.h"

struct Ms3SignalValue {
  SignalId id;
  float phys;
};

class Ms3Decoder {
 public:
  Ms3Decoder() = default;
  bool decode(const twai_message_t& msg, Ms3SignalValue* out, uint8_t& count) const;

 private:
};

bool RunMs3DecodeGoldenTest(const Ms3Decoder& decoder);
