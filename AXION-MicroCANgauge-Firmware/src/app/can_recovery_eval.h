#pragma once

#include <stdint.h>

struct CanScanEvalIn {
  bool started_ok = false;
  bool normal_mode = false;
  uint32_t rx_dash = 0;
  uint32_t min_dash = 0;
  uint32_t bus_off = 0;
  uint32_t err_passive = 0;
  uint32_t rx_overrun = 0;
  uint32_t rx_missed = 0;
};

bool CanScanEvalOk(const CanScanEvalIn& in, uint32_t max_missed);
