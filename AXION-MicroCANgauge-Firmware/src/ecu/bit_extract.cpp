#include "ecu/bit_extract.h"

#include "ecu/bit_order.h"

namespace {

// Motorola / DBC "sawtooth" extraction.
// - DBC bit numbering: bit0 is the LSB of byte0.
// - startBit points to the MSB of the signal.
// - Bit walk: move toward lower bit numbers inside the byte; after bit0 the
//   next bit is bit7 of the following byte (bit_index += 15).
uint64_t extractMotorolaDBC(const uint8_t* data, int startBit, int length) {
  if (length <= 0 || length > 64) return 0;

  int bit_index = startBit;
  uint64_t value = 0;

  for (int i = 0; i < length; ++i) {
    if (bit_index < 0) break;
    const int byte_index = bit_index / 8;
    if (byte_index < 0) break;
    const int bit_in_byte = bit_index % 8;
    const uint8_t bit_val = (data[byte_index] >> bit_in_byte) & 0x1U;
    value = (value << 1) | static_cast<uint64_t>(bit_val);

    // Advance along the sawtooth.
    if (bit_in_byte == 0) {
      bit_index += 15;  // jump to bit7 of next byte
    } else {
      --bit_index;
    }
  }
  return value;
}

uint64_t extractIntelLE(const uint8_t* data, int startBit, int length) {
  uint64_t value = 0;
  for (int i = 0; i < length; ++i) {
    const int bit_pos = startBit + i;
    const int byte_index = bit_pos / 8;
    const int bit_index = bit_pos % 8;
    const uint8_t bit_val = (data[byte_index] >> bit_index) & 0x1U;
    value |= (static_cast<uint64_t>(bit_val) << i);
  }
  return value;
}

}  // namespace

uint64_t extractBits(const uint8_t* data, int startBit, int length,
                     BitOrder order) {
  if (!data || length <= 0 || length > 64 || startBit < 0) return 0;
  switch (order) {
    case BitOrder::MotorolaDBC:
      return extractMotorolaDBC(data, startBit, length);
    case BitOrder::IntelLE:
      return extractIntelLE(data, startBit, length);
    default:
      return 0;
  }
}
