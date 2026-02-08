#include "data/datastore.h"
#include "data/signal_contract.h"

DataStore::DataStore() {
  for (size_t i = 0; i < static_cast<size_t>(SignalId::kCount); ++i) {
    invalid_until_ms_[i] = 0;
    seq_[i] = 0;
  }
}

void DataStore::update(SignalId id, float phys, uint32_t now_ms, uint8_t flags) {
  const size_t idx = static_cast<size_t>(id);
  if (idx >= static_cast<size_t>(SignalId::kCount)) {
    return;
  }
  ValidateSignalContract(id, phys);
  volatile uint32_t& seq = seq_[idx];
  seq += 1;  // enter (odd)
  values_[idx].value = phys;
  values_[idx].ts_ms = now_ms;
  values_[idx].flags = flags;
  invalid_until_ms_[idx] = 0;
  seq += 1;  // exit (even)
}

SignalRead DataStore::get(SignalId id, uint32_t now_ms) const {
  SignalRead out{};
  const size_t idx = static_cast<size_t>(id);
  if (idx >= static_cast<size_t>(SignalId::kCount)) {
    return out;
  }
  for (;;) {
    uint32_t seq_begin = seq_[idx];
    if (seq_begin & 0x1U) continue;  // writer in progress
    SignalValue v = values_[idx];
    uint32_t invalid_until = invalid_until_ms_[idx];
    uint32_t seq_end = seq_[idx];
    if (seq_begin != seq_end || (seq_end & 0x1U)) {
      continue;  // inconsistent read; retry
    }

    out.value = v.value;
    out.valid = v.ts_ms != 0;
    if (out.valid) {
      uint32_t age = now_ms - v.ts_ms;  // unsigned: preserves wrap-around
      if (now_ms < v.ts_ms) {
        const uint32_t skew = v.ts_ms - now_ms;
        if (skew <= 2000U) {
          age = 0;
        }
      }
      out.age_ms = age;
    } else {
      out.age_ms = 0xFFFFFFFFu;
    }
    out.flags = v.flags;
    if (now_ms < invalid_until) {
      out.valid = false;
      out.flags |= kFlagInvalid;
      return out;
    }
    if (!out.valid) {
      return out;
    }
    if (v.expire_ms > 0 && out.age_ms > v.expire_ms) {
      out.valid = false;
      out.flags |= kFlagStale;
      return out;
    }
    if (out.age_ms > v.stale_ms) {
      out.flags |= kFlagStale;
    }
    return out;
  }
}

void DataStore::setStaleMs(SignalId id, uint32_t stale_ms) {
  const size_t idx = static_cast<size_t>(id);
  if (idx >= static_cast<size_t>(SignalId::kCount)) {
    return;
  }
  values_[idx].stale_ms = stale_ms;
}

void DataStore::setDefaultStale(uint32_t stale_ms) {
  for (size_t i = 0; i < static_cast<size_t>(SignalId::kCount); ++i) {
    values_[i].stale_ms = stale_ms;
  }
}

void DataStore::setStaleForSignals(const SignalId* ids, uint8_t count,
                                   uint32_t stale_ms) {
  if (!ids || count == 0) return;
  for (uint8_t i = 0; i < count; ++i) {
    setStaleMs(ids[i], stale_ms);
  }
}

void DataStore::note_invalid(SignalId id, uint32_t now_ms, uint32_t hold_ms) {
  const size_t idx = static_cast<size_t>(id);
  if (idx >= static_cast<size_t>(SignalId::kCount)) {
    return;
  }
  volatile uint32_t& seq = seq_[idx];
  seq += 1;  // enter (odd)
  invalid_until_ms_[idx] = now_ms + hold_ms;
  seq += 1;  // exit (even)
}
