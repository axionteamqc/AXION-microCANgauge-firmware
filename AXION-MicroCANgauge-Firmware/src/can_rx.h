#pragma once

#include <Arduino.h>
#include "driver/twai.h"

struct CanRxSnapshot {
  struct Entry {
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {0};
    uint32_t ts_ms = 0;
    bool valid = false;
  } entries[5];
};

struct CanRxCounters {
  uint32_t rx_total = 0;
  uint32_t rx_match = 0;
  uint32_t rx_dash = 0;
  uint32_t rx_overrun = 0;
  uint32_t rx_timeout = 0;
  uint32_t rx_err_passive = 0;
  uint32_t decode_oob = 0;
};

// Initialize/reset storage.
void canrx_init();

// Placeholder for future task start; currently no background thread.
void canrx_start();

// Record a single frame (latest-only per ID).
void canrx_record(const twai_message_t& msg, uint32_t now_ms);

// Thread-safe snapshot of the last frames for IDs 0x5E8..0x5EC.
void canrx_get_snapshot(CanRxSnapshot& out);

// Thread-safe copy of counters.
void canrx_get_counters(CanRxCounters& out);
