#pragma once

#include <stddef.h>
#include <stdint.h>

enum class I2cOledAction : uint8_t {
  kProbe = 0,
  kRecover = 1,
  kWireReset = 2,
  kInit = 3,
  kClear = 4,
};

struct I2cOledLogEntry {
  uint32_t ms = 0;
  uint8_t oled = 0;   // 1 or 2
  uint8_t action = 0; // I2cOledAction
  uint8_t ok = 0;
  uint8_t sda = 0xFF;
  uint8_t scl = 0xFF;
};

struct I2cOledLogSnapshot {
  uint16_t version = 1;
  uint16_t count = 0;
  uint32_t saved_ms = 0;
  I2cOledLogEntry entries[64];
};

void I2cOledLogEvent(uint8_t oled, I2cOledAction action, bool ok,
                     uint8_t sda_pin, uint8_t scl_pin);
uint16_t I2cOledLogCopy(I2cOledLogEntry* out, uint16_t max_entries);
const char* I2cOledActionStr(I2cOledAction action);
bool I2cOledLogSaveSnapshot(uint32_t now_ms);
bool I2cOledLogLoadSnapshot(I2cOledLogSnapshot& out);
uint16_t I2cOledLogCount();
