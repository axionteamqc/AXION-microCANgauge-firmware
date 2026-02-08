#pragma once

#include <Arduino.h>
#if ARDUINO_USB_CDC_ON_BOOT
#include <USB.h>  // ensure Serial is declared when using native USB CDC
#ifndef CONFIG_TINYUSB_CDC_ENABLED
// Fallback: if TinyUSB CDC is disabled, route Serial to UART0.
#define Serial Serial0
#endif
#endif

namespace Pins {
constexpr uint8_t kI2cSda = 8;
constexpr uint8_t kI2cScl = 9;
constexpr uint8_t kCanTx = 7;
constexpr uint8_t kCanRx = 6;
constexpr uint8_t kButton = 3;
constexpr uint8_t kI2c2Sda = 4;
constexpr uint8_t kI2c2Scl = 5;
}  // namespace Pins
