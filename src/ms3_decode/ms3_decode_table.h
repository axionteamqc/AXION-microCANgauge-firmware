#pragma once

#include <Arduino.h>

#include "data/datastore.h"
#include "ecu/bit_order.h"

struct Ms3SignalSpec {
  SignalId id;
  uint8_t start_bit;
  uint8_t length;
  bool is_signed;
  float scale;
  float offset;
  BitOrder bit_order;
};

struct Ms3MessageSpec {
  uint32_t can_id;
  const Ms3SignalSpec* signals;
  uint8_t signal_count;
};

extern const Ms3MessageSpec kMs3Messages[];
extern const size_t kMs3MessageCount;
