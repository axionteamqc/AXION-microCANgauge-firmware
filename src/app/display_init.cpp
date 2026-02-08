#include <Arduino.h>
#include <Wire.h>

#include "app/display_init.h"
#include "app_config.h"
#include "config/logging.h"
#include "pins.h"

namespace {

OledU8g2::Profile ConfiguredProfile() {
  return (AppConfig::kOledInitProfile == AppConfig::OledInitProfile::kWinstar)
             ? OledU8g2::Profile::kWinstar
             : OledU8g2::Profile::kUnivision;
}

}  // namespace

bool ScanI2cBus(uint8_t sda, uint8_t scl, const char* label) {
  Wire.begin(sda, scl, AppConfig::kI2cFrequencyHz);
  if (kEnableVerboseSerialLogs) {
    LOGI("[I2C] scan %s SDA=%u SCL=%u ...\r\n", label,
         static_cast<unsigned>(sda), static_cast<unsigned>(scl));
  }
  uint8_t found = 0;
  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      if (kEnableVerboseSerialLogs) {
        LOGI("  - 0x%02X\r\n", addr);
      }
      ++found;
    }
  }
  if (!found) {
    if (kEnableVerboseSerialLogs) {
      LOGI("  (aucun peripherique)\r\n");
    }
  }
  return found > 0;
}

bool ScanI2cBuses() {
  bool any = false;
  any |= ScanI2cBus(Pins::kI2cSda, Pins::kI2cScl, "OLED1 bus");
  any |= ScanI2cBus(Pins::kI2c2Sda, Pins::kI2c2Scl, "OLED2 bus");
  return any;
}

bool initAndTestDisplay(OledU8g2& oled, bool is_hw_bus, uint32_t bus_hz) {
  if (is_hw_bus) {
    return oled.begin(bus_hz, ConfiguredProfile(), true);
  }
  return oled.begin(bus_hz, ConfiguredProfile(), false);
}

bool initAndTestDisplay64(OledU8g2& oled, bool is_hw_bus, uint32_t bus_hz) {
  if (!is_hw_bus) {
    return false;
  }
  return oled.begin64(bus_hz, true);
}
