#pragma once

#include <stdint.h>

#include "app_state.h"

struct CanStateSnapshot {
  bool can_ready = false;
  bool can_bitrate_locked = false;
  uint32_t last_can_rx_ms = 0;
  uint32_t last_can_match_ms = 0;
  uint8_t twai_state = 0;
  uint8_t tec = 0;
  uint8_t rec = 0;
  AppState::CanStats can_stats;
  AppState::CanRateWindow can_rates;
  uint32_t last_bus_off_ms = 0;
};

void GetCanStateSnapshot(CanStateSnapshot& out);
