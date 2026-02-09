#include "app/i2c_oled_log.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

constexpr uint16_t kLogCapacity = 64;
static I2cOledLogEntry g_log[kLogCapacity];
static uint16_t g_log_head = 0;
static uint16_t g_log_count = 0;

constexpr const char* kNvsNamespace = "cangauge";
constexpr const char* kNvsKeyI2cLog = "i2c_log";

inline uint8_t ReadPin(uint8_t pin) {
  if (pin == 0xFF) return 0xFF;
  return digitalRead(pin) ? 1 : 0;
}

}  // namespace

void I2cOledLogEvent(uint8_t oled, I2cOledAction action, bool ok,
                     uint8_t sda_pin, uint8_t scl_pin) {
  I2cOledLogEntry e{};
  e.ms = millis();
  e.oled = oled;
  e.action = static_cast<uint8_t>(action);
  e.ok = ok ? 1 : 0;
  e.sda = ReadPin(sda_pin);
  e.scl = ReadPin(scl_pin);
  g_log[g_log_head] = e;
  g_log_head = static_cast<uint16_t>((g_log_head + 1U) % kLogCapacity);
  if (g_log_count < kLogCapacity) {
    ++g_log_count;
  }
}

uint16_t I2cOledLogCopy(I2cOledLogEntry* out, uint16_t max_entries) {
  if (!out || max_entries == 0) return 0;
  const uint16_t count = (g_log_count < max_entries) ? g_log_count : max_entries;
  const uint16_t start =
      static_cast<uint16_t>((g_log_head + kLogCapacity - g_log_count) %
                            kLogCapacity);
  for (uint16_t i = 0; i < count; ++i) {
    const uint16_t idx =
        static_cast<uint16_t>((start + i) % kLogCapacity);
    out[i] = g_log[idx];
  }
  return count;
}

uint16_t I2cOledLogCount() {
  return g_log_count;
}

const char* I2cOledActionStr(I2cOledAction action) {
  switch (action) {
    case I2cOledAction::kProbe:
      return "probe";
    case I2cOledAction::kRecover:
      return "recover";
    case I2cOledAction::kWireReset:
      return "wire_reset";
    case I2cOledAction::kInit:
      return "init";
    case I2cOledAction::kClear:
      return "clear";
    default:
      return "unknown";
  }
}

bool I2cOledLogSaveSnapshot(uint32_t now_ms) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, false)) {
    return false;
  }
  I2cOledLogSnapshot snap{};
  snap.version = 1;
  snap.saved_ms = now_ms;
  snap.count = I2cOledLogCopy(snap.entries, kLogCapacity);
  const size_t written = prefs.putBytes(kNvsKeyI2cLog, &snap, sizeof(snap));
  prefs.end();
  return (written == sizeof(snap));
}

bool I2cOledLogLoadSnapshot(I2cOledLogSnapshot& out) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, true)) {
    return false;
  }
  const size_t read = prefs.getBytes(kNvsKeyI2cLog, &out, sizeof(out));
  prefs.end();
  return (read == sizeof(out) && out.version == 1);
}
