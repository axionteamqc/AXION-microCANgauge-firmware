#pragma once
// Factory-level configuration: centralize branding and ECU selection.
// Notes:
// - ASCII only
// - Keep strings reasonably short for 128x32/64 displays (12-16 chars recommended)
#include "app_config.h"
#include "config/build_version.h"

enum class EcuTarget : uint8_t {
  kMs3EvoPlus = 0,
  // Future targets: kHaltech, kMaxx, kLink, ...
};

// Target ECU for this build.
constexpr EcuTarget kEcuTarget = EcuTarget::kMs3EvoPlus;

// Firmware metadata (for future About/Info screens/logs).
#ifdef AXION_FW_VERSION
constexpr const char* kFirmwareVersion = AXION_FW_VERSION;
#else
constexpr const char* kFirmwareVersion = BuildVersion::kFwVersion;
#endif
#ifdef AXION_BUILD_ID
constexpr const char* kBuildId = AXION_BUILD_ID;
#else
constexpr const char* kBuildId = "dev";
#endif

// Boot / hello texts shown during startup animations.
constexpr const char* kBootBrandText = "AXION";
constexpr const char* kBootHelloLine1 = "Micro";
constexpr const char* kBootHelloLine2 = "CanGauge";

// Provisioning toggles (defaults preserve current behavior).
// Ship in simulation mode instead of CAN? (true keeps mock data active)
constexpr bool kShipWithSimulationEnabled = (AppConfig::kDataSource == AppConfig::DataSource::kMock);
// Enable ECU detect debug paths (still MS3-only; off by default).
[[maybe_unused]] constexpr bool kEnableEcuDetectDebug = false;  // reserved / not used in V38
// Verbose serial logs (default false; override with -DAXION_VERBOSE_LOGS=1).
#ifndef AXION_VERBOSE_LOGS
#define AXION_VERBOSE_LOGS 0
#endif
constexpr bool kEnableVerboseSerialLogs = (AXION_VERBOSE_LOGS != 0);
// Periodic diagnostics/heartbeat logs (HB/DNS/WiFi slice); keep off for production.
constexpr bool kEnablePeriodicDiagLogs = false;
// Periodic serial heartbeat to confirm firmware is alive (2s cadence).
constexpr bool kEnableHeartbeatSerial = true;
// Lock display topology (prevent user changes) - not wired yet by default.
[[maybe_unused]] constexpr bool kFactoryLockDisplayTopology = false;  // reserved / not used in V38
// Extra settle time after Wire.begin before OLED bring-up (ms).
constexpr uint32_t kBootI2cSettleDelayMs = 80;
// Optional delay before the initial boot I2C scan (ms). Set 0 to disable.
constexpr uint32_t kBootI2cPreScanDelayMs = 0;
// Optional extra settle time immediately before OLED init (ms). Set 0 to disable.
constexpr uint32_t kBootI2cPreOledDelayMs = 0;
// Allow repeated I2C scans after boot (disabled to avoid runtime jitter).
constexpr bool kEnableBootI2cScan = false;
// Long-press fail-safe restart delay (ms). Set 0 to disable.
constexpr uint32_t kFailsafeRestartHoldMs = 10000;
// SSE update interval for live portal (ms).
constexpr uint32_t kWifiSseIntervalMs = 500;
// Auto-enter Wi-Fi mode if no OLED is detected for this long after boot (ms). Set 0 to disable.
constexpr uint32_t kAutoWifiNoOledDelayMs = 10000;
// Optional delay before a one-shot OLED init retry if none are detected (ms). Set 0 to disable.
constexpr uint32_t kBootOledRetryDelayMs = 0;
// If CAN is unlocked and NO_FRAMES persists this long, trigger a rescan (ms). Set 0 to disable.
constexpr uint32_t kCanUnlockedRescanNoFramesMs = 15000;

// Portal heap diagnostics (compile-time). Override with -DWIFI_PORTAL_HEAP_DIAG=1.
#ifndef WIFI_PORTAL_HEAP_DIAG
#define WIFI_PORTAL_HEAP_DIAG 0
#endif
