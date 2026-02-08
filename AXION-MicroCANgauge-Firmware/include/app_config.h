#pragma once

#include <Arduino.h>

#include "debug/debug_flags.h"
#include "pins.h"

#ifndef SETUP_WIZARD_ENABLED
#define SETUP_WIZARD_ENABLED 0
#endif
#ifndef BARO_DEBUG
#define BARO_DEBUG 0
#endif

namespace AppConfig {

constexpr uint32_t kI2cFrequencyHz = 100000;

constexpr uint8_t kOledWidth = 128;
constexpr uint8_t kOledHeight = 32;
constexpr int8_t kOledResetPin = -1;  // -1 means not connected
constexpr uint8_t kOledAddressPrimary = 0x3C;
// Both modules are fixed 0x3C; secondary kept for completeness.
constexpr uint8_t kOledAddressSecondary = 0x3C;

enum class OledInitProfile { kUnivision = 0, kWinstar = 1 };
constexpr OledInitProfile kOledInitProfile = OledInitProfile::kUnivision;

constexpr bool kOled2NoPullupsProtoFallback = false;  // enable only for early prototypes without pullups
// Primary SW I2C speed for OLED2 (prototype best-effort). Retry logic remains
// in the driver if a slower fallback is needed.
constexpr uint32_t kI2c2FrequencyHz = 100000;

constexpr bool kUiSelfTestEnabled = false;
constexpr uint16_t kUiSelfTestPeriodMs = 700;

constexpr bool kDecodeSelfTestEnabled = false;
// Setup wizard is disabled for release builds; enable only when explicitly needed.
constexpr bool kSetupWizardEnabled = (SETUP_WIZARD_ENABLED != 0);

constexpr uint16_t kButtonDebounceMs = 20;
constexpr uint16_t kButtonMultiClickWindowMs = 300;
constexpr uint16_t kButtonLongPressMs = 1000;
constexpr uint16_t kButtonMinPressMs = 20;

enum class BigValueFont {
  kInb33,
  kLogiso34,
  kLogiso32,
};

constexpr BigValueFont kBigValueFont = BigValueFont::kInb33;
constexpr int8_t kBigValueBaselineNudgePx = 7;         // move big digits down
constexpr int8_t kErrorValueBaselineNudgePx = 1;       // error text sits higher
constexpr int16_t kBigValueBaselineMaxExtraPx = 12;    // allow relaxed downward clamp
[[maybe_unused]] constexpr bool kOled2AtomicUpdate = false;  // reserved / not used in V38

enum class DataSource {
  kMock = 0,
  kCan = 1,
};

// Compile-time gate for real CAN runtime. If false, firmware always uses mock data
// regardless of UI toggles.
constexpr bool kCanRuntimeSupported = true;
// ONE-LINE SWITCH: set kDataSource to kCan to use real CAN frames, otherwise mock.
constexpr DataSource kDataSource = DataSource::kCan;
constexpr bool kUseRealCanData = (kDataSource == DataSource::kCan);
constexpr bool IsRealCanEnabled() { return kDataSource == DataSource::kCan; }
constexpr float kDefaultBaroKpa = 101.3f;

}  // namespace AppConfig
