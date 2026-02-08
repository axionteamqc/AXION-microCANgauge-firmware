#pragma once

#include <Arduino.h>
#include "driver/twai.h"

#include "ms3_decode/ms3_decode.h"

// Temporary alias until multiple ECU profiles are implemented.
using DecodedSignal = Ms3SignalValue;

struct DashSpec {
  const uint32_t* ids = nullptr;
  uint8_t count = 0;
  uint8_t require_min_rx_dash = 0;
  uint32_t required_first_id = 0;
};

enum class AutobaudMode : uint8_t {
  kProfileSpecific = 0,  // filter/score using profile IDs (current MS3 behavior)
  kGeneric = 1,          // listen-only without assuming specific IDs (future use)
};

struct ValidationSpec {
  uint32_t window_ms = 0;
  uint16_t min_rx_dash = 0;
};

struct AutobaudSpec {
  AutobaudMode mode = AutobaudMode::kProfileSpecific;
  bool allow_listen_only = true;  // if false, prefer immediate NORMAL with ACK peer
  uint32_t normal_probe_window_ms = 0;  // short NORMAL probe per bitrate when listen-only disallowed
  const uint32_t* bitrates = nullptr;
  uint8_t bitrate_count = 0;
  uint32_t listen_window_ms = 0;
  uint32_t confirm_window_ms = 0;
  uint8_t confirm_windows = 0;
  uint16_t min_rx_total = 0;
  uint16_t min_rx_dash = 0;
  uint16_t confirm_min_frames = 0;
  uint8_t confirm_min_distinct_ids = 0;
  uint16_t confirm_min_expected_hits = 0;
  uint8_t require_min_per_id = 0;
  uint8_t require_distinct_ids = 0;
  uint8_t max_rx_missed = 0;
  bool require_bus_off_clear = true;
};

struct SignalSpan {
  const SignalId* ids = nullptr;
  uint8_t count = 0;
};

class IEcuProfile {
 public:
  virtual ~IEcuProfile() = default;

  virtual const char* name() const = 0;

  // Frame filtering
  virtual bool acceptFrame(const twai_message_t& msg) const = 0;
  virtual bool acceptId(uint32_t id) const = 0;

  // Decode: fill out[] and set count
  virtual bool decode(const twai_message_t& msg, DecodedSignal* out,
                      uint8_t& count) const = 0;

  // Dash identifiers / mask helpers
  virtual const DashSpec& dashSpec() const = 0;
  virtual int dashIndexForId(uint32_t id) const = 0;  // -1 if unexpected
  virtual uint8_t dashIdCount() const = 0;
  virtual uint32_t dashIdAt(uint8_t i) const = 0;
  virtual const ValidationSpec& validationSpec() const = 0;
  virtual SignalSpan dashSignalsForIndex(uint8_t idx) const = 0;
  virtual const AutobaudSpec& autobaudSpec() const = 0;

  // Bitrate list / fixed bitrate
  virtual const uint32_t* scanBitrates(uint8_t& count) const = 0;
  virtual bool hasFixedBitrate() const = 0;
  virtual uint32_t fixedBitrate() const = 0;
  virtual uint32_t preferredBitrate() const = 0;  // fallback: first scan bitrate or fixed
  virtual bool requiresAckPeer() const = 0;
};
