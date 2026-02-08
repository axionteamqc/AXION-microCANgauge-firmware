#pragma once

#include <stdint.h>

#include "ecu/bit_order.h"

// Generic bit extractor supporting DBC Motorola (sawtooth) and Intel (little
// endian) layouts. Returns the raw unsigned value (up to 64 bits).
uint64_t extractBits(const uint8_t* data, int startBit, int length,
                     BitOrder order);
