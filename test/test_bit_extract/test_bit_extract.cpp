#include <stdint.h>
#include <unity.h>

#include "ecu/bit_extract.h"
#include "ecu/bit_order.h"

// Reference packer for Motorola DBC ("sawtooth") numbering.
// startBit is the MSB of the signal. bit0 is the LSB of byte0.
static void packMotorolaDBC(uint8_t* frame, int startBit, int length,
                            uint32_t raw) {
  memset(frame, 0, 8);
  int bit_index = startBit;
  for (int i = 0; i < length; ++i) {
    const int bit_pos_from_msb = length - 1 - i;  // raw bit to place
    const uint8_t bit_val = (raw >> bit_pos_from_msb) & 0x1U;
    const int byte_index = bit_index / 8;
    const int bit_in_byte = bit_index % 8;
    frame[byte_index] |= (bit_val << bit_in_byte);
    // advance sawtooth
    if (bit_in_byte == 0) {
      bit_index += 15;
    } else {
      --bit_index;
    }
  }
}

// Reference packer for Intel (little-endian) bit numbering (DBC style).
static void packIntelLE(uint8_t* frame, int startBit, int length,
                        uint32_t raw) {
  memset(frame, 0, 8);
  for (int i = 0; i < length; ++i) {
    const uint8_t bit_val = (raw >> i) & 0x1U;
    const int bit_pos = startBit + i;
    const int byte_index = bit_pos / 8;
    const int bit_in_byte = bit_pos % 8;
    frame[byte_index] |= (bit_val << bit_in_byte);
  }
}

struct Case {
  const char* name;
  int start;
  int len;
  uint32_t raw;
};

static const Case kCases[] = {
    {"MAP/BATT/VSS1 7|16", 7, 16, 0x1234},
    {"RPM 23|16", 23, 16, 0x5678},
    {"CLT 39|16", 39, 16, 0x9ABC},
    {"TPS 55|16", 55, 16, 0x0DEF},
    {"AFR1 15|8", 15, 8, 0x5A},
    {"AFR_TGT 7|8", 7, 8, 0xC3},
    {"KNK 55|8", 55, 8, 0x7E},
};

void test_motorola_pack_and_extract(void) {
  uint8_t frame[8];
  for (const auto& c : kCases) {
    packMotorolaDBC(frame, c.start, c.len, c.raw);
    uint32_t out =
        static_cast<uint32_t>(extractBits(frame, c.start, c.len,
                                          BitOrder::MotorolaDBC));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.raw, out, c.name);
  }
}

void test_intel_placeholder(void) {
  const uint8_t frame[2] = {0x12, 0x34};
  TEST_ASSERT_EQUAL_UINT32(
      0x12, extractBits(frame, 0, 8, BitOrder::IntelLE));
  TEST_ASSERT_EQUAL_UINT32(
      0x3412, extractBits(frame, 0, 16, BitOrder::IntelLE));

  // Pack/unpack round trips
  struct IntelCase {
    int start;
    int len;
    uint32_t raw;
  } icases[] = {{0, 16, 0xA1B2}, {8, 16, 0xC3D4}, {12, 12, 0xABC},
                {31, 16, 0x1234}};
  uint8_t buf[8];
  for (const auto& c : icases) {
    packIntelLE(buf, c.start, c.len, c.raw);
    uint32_t out =
        static_cast<uint32_t>(extractBits(buf, c.start, c.len, BitOrder::IntelLE));
    TEST_ASSERT_EQUAL_UINT32(c.raw, out);
  }

  // Cross-check that Motorola and Intel differ on the same bytes
  uint8_t cross[8] = {0};
  packIntelLE(cross, 0, 16, 0xBEEF);
  const uint32_t intel_val =
      static_cast<uint32_t>(extractBits(cross, 0, 16, BitOrder::IntelLE));
  const uint32_t motorola_val =
      static_cast<uint32_t>(extractBits(cross, 15, 16, BitOrder::MotorolaDBC));
  TEST_ASSERT_NOT_EQUAL(intel_val, motorola_val);
}

void setUp(void) {}
void tearDown(void) {}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_motorola_pack_and_extract);
  RUN_TEST(test_intel_placeholder);
  UNITY_END();
}

void loop() {}
