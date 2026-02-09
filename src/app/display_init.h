#pragma once

#include <stdint.h>

#include "drivers/oled_u8g2.h"

bool ScanI2cBus(uint8_t sda, uint8_t scl, const char* label);
bool ScanI2cBuses();
bool initAndTestDisplay(OledU8g2& oled, bool is_hw_bus, uint32_t bus_hz);
bool initAndTestDisplay64(OledU8g2& oled, bool is_hw_bus, uint32_t bus_hz);
bool RecoverI2cBus(uint8_t sda, uint8_t scl, const char* label,
                   bool log_verbose, bool force = false);
