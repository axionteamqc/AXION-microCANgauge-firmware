#pragma once

#include <Arduino.h>

#include "can_link/twai_link.h"
#include "settings/nvs_store.h"

#include "ecu/ecu_profile.h"

struct AutoBaudStats {
  uint32_t bitrate = 0;
  uint32_t rx_total = 0;
  uint32_t rx_dash = 0;
  uint32_t id_counts[5] = {0, 0, 0, 0, 0};
  uint32_t bus_off = 0;
  uint32_t err_passive = 0;
  uint32_t rx_overrun = 0;
  uint32_t rx_missed = 0;
  uint8_t id_present_mask = 0;
  bool locked = false;
};

struct AutoBaudScore {
  uint32_t bitrate = 0;
  uint32_t rx_total = 0;
  uint8_t distinct_ids = 0;
  uint32_t expected_ids_hits = 0;
  uint32_t errors = 0;
  uint32_t missed = 0;
  uint32_t overruns = 0;
  int32_t score = 0;
};

enum class AutoBaudResult : uint8_t {
  kOk = 0,
  kNoActivity,
  kNoClearWinner,
  kConfirmFailed,
  kNormalVerifyFailed,
};

struct AutoBaudDiag {
  static constexpr size_t kMaxEntries = 4;
  AutoBaudScore entries[kMaxEntries]{};
  uint8_t entry_count = 0;
  AutoBaudResult reason = AutoBaudResult::kNoActivity;
};

AutoBaudResult RunAutoBaudScan(const IEcuProfile& profile, TwaiLink& link, AutoBaudStats& result,
                               CanSettings& locked_settings, AutoBaudDiag* diag_out);
