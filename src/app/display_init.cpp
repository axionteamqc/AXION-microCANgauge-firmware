#include <Arduino.h>
#include <Wire.h>

#include "app/display_init.h"
#include "app/i2c_oled_log.h"
#include "app/app_globals.h"
#include "app_config.h"
#include "config/logging.h"
#include "pins.h"

namespace {

OledU8g2::Profile ConfiguredProfile() {
  return (AppConfig::kOledInitProfile == AppConfig::OledInitProfile::kWinstar)
             ? OledU8g2::Profile::kWinstar
             : OledU8g2::Profile::kUnivision;
}

inline uint32_t I2cHalfPeriodUs() {
  uint32_t us = GetSwI2cDelayUs();
  if (us > 0) return us;
  const uint32_t safe_hz = AppConfig::kSafeCableBootI2cHz;
  if (safe_hz == 0) return 5;
  const uint32_t half_us = 500000UL / safe_hz;
  return (half_us > 0) ? half_us : 1;
}

inline void I2cDelayShort() { delayMicroseconds(I2cHalfPeriodUs()); }

inline uint8_t OledIdFromPins(uint8_t sda, uint8_t scl) {
  if (sda == Pins::kI2cSda && scl == Pins::kI2cScl) return 1;
  if (sda == Pins::kI2c2Sda && scl == Pins::kI2c2Scl) return 2;
  return 0;
}

}  // namespace

bool RecoverI2cBus(uint8_t sda, uint8_t scl, const char* label,
                   bool log_verbose, bool force) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  I2cDelayShort();
  if (!force && digitalRead(sda) != LOW) {
    return true;
  }
  if (log_verbose) {
    LOGW("I2C %s recovery%s\r\n", label, force ? " (forced)" : "");
  }
  const uint32_t start_us = micros();
  while (digitalRead(scl) == LOW) {
    if ((micros() - start_us) > 2000U) {
      if (log_verbose) {
        LOGW("I2C %s SCL stuck low\r\n", label);
      }
      break;
    }
    I2cDelayShort();
  }
  for (uint8_t i = 0; i < 32; ++i) {
    pinMode(scl, OUTPUT);
    digitalWrite(scl, LOW);
    I2cDelayShort();
    pinMode(scl, INPUT_PULLUP);
    I2cDelayShort();
  }
  // STOP: SDA low -> high while SCL high.
  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);
  I2cDelayShort();
  pinMode(scl, INPUT_PULLUP);
  I2cDelayShort();
  pinMode(sda, INPUT_PULLUP);
  I2cDelayShort();
  const bool ok = (digitalRead(sda) != LOW);
  if (!ok && log_verbose) {
    LOGW("I2C %s SDA still low after recovery\r\n", label);
  }
  I2cOledLogEvent(OledIdFromPins(sda, scl), I2cOledAction::kRecover, ok, sda, scl);
  return ok;
}

bool ScanI2cBus(uint8_t sda, uint8_t scl, const char* label) {
  Wire.begin(sda, scl, AppConfig::kBootI2cClockHz);
  g_wire_sda_pin = sda;
  g_wire_scl_pin = scl;
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
  const bool oled2_ack = g_oled_secondary.probeAddress();
  if (kEnableVerboseSerialLogs) {
    LOGI("[I2C] scan OLED2 bus (SW) ACK=%s\r\n", oled2_ack ? "ok" : "fail");
  }
  any |= oled2_ack;
  return any;
}

bool initAndTestDisplay(OledU8g2& oled, bool /*is_hw_bus*/, uint32_t bus_hz) {
  const bool ok = oled.begin(bus_hz, ConfiguredProfile(), true);
  const uint8_t oled_id = (oled.bus() == OledU8g2::Bus::kHw) ? 1 : 2;
  const uint8_t sda = (oled_id == 1) ? Pins::kI2cSda : Pins::kI2c2Sda;
  const uint8_t scl = (oled_id == 1) ? Pins::kI2cScl : Pins::kI2c2Scl;
  I2cOledLogEvent(oled_id, I2cOledAction::kInit, ok, sda, scl);
  return ok;
}

bool initAndTestDisplay64(OledU8g2& oled, bool is_hw_bus, uint32_t bus_hz) {
  if (!is_hw_bus) {
    return false;
  }
  const bool ok = oled.begin64(bus_hz, true);
  const uint8_t sda = Pins::kI2cSda;
  const uint8_t scl = Pins::kI2cScl;
  I2cOledLogEvent(1, I2cOledAction::kInit, ok, sda, scl);
  return ok;
}
