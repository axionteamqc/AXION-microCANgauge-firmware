#pragma once

#include <stddef.h>
#include <stdint.h>

enum class SignalId : uint8_t {
  kMap = 0,
  kClt,
  kRpm,
  kTps,
  kMat,
  kAdv,
  kPw1,
  kPw2,
  kPwSeq1,
  kEgoCor1,
  kAfr1,
  kAfrTarget1,
  kEgt1,
  kBatt,
  kKnkRetard,
  kSensors1,
  kSensors2,
  kLaunchTiming,
  kTcRetard,
  kVss1,
  kCount
};

constexpr uint8_t kFlagStale = 0x01;
constexpr uint8_t kFlagInvalid = 0x02;

struct SignalValue {
  float value = 0.0f;
  uint32_t ts_ms = 0;
  uint32_t stale_ms = 500;
  uint32_t expire_ms = 5000;
  uint8_t flags = 0;
};

struct SignalRead {
  float value = 0.0f;
  bool valid = false;
  uint32_t age_ms = 0;
  uint8_t flags = 0;
};

class DataStore {
 public:
  DataStore();

  void update(SignalId id, float phys, uint32_t now_ms, uint8_t flags = 0);
  SignalRead get(SignalId id, uint32_t now_ms) const;
  void setStaleMs(SignalId id, uint32_t stale_ms);
  void setStaleForSignals(const SignalId* ids, uint8_t count,
                          uint32_t stale_ms);
  void setDefaultStale(uint32_t stale_ms);
  void note_invalid(SignalId id, uint32_t now_ms, uint32_t hold_ms = 1500);
#ifdef UNIT_TEST
  uint32_t debug_seq(SignalId id) const {
    const size_t idx = static_cast<size_t>(id);
    return (idx < static_cast<size_t>(SignalId::kCount)) ? seq_[idx] : 0;
  }
#endif

 private:
  SignalValue values_[static_cast<size_t>(SignalId::kCount)];
  uint32_t invalid_until_ms_[static_cast<size_t>(SignalId::kCount)];
  volatile uint32_t seq_[static_cast<size_t>(SignalId::kCount)];
};
