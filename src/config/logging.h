#pragma once

#include <Arduino.h>

#include "config/factory_config.h"

#ifndef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL 0
#endif

#if CORE_DEBUG_LEVEL >= 4
#define LOGV(fmt, ...) \
  do { \
    if (kEnableVerboseSerialLogs) { \
      Serial.printf((fmt), ##__VA_ARGS__); \
    } \
  } while (0)
#else
#define LOGV(...) do { } while (0)
#endif

#if CORE_DEBUG_LEVEL >= 4
#define LOGD(fmt, ...) \
  do { \
    Serial.printf((fmt), ##__VA_ARGS__); \
  } while (0)
#else
#define LOGD(...) do { } while (0)
#endif

#if CORE_DEBUG_LEVEL >= 3
#define LOGI(fmt, ...) \
  do { \
    Serial.printf((fmt), ##__VA_ARGS__); \
  } while (0)
#else
#define LOGI(...) do { } while (0)
#endif

#if CORE_DEBUG_LEVEL >= 2
#define LOGW(fmt, ...) \
  do { \
    Serial.printf((fmt), ##__VA_ARGS__); \
  } while (0)
#else
#define LOGW(...) do { } while (0)
#endif

#if CORE_DEBUG_LEVEL >= 1
#define LOGE(fmt, ...) \
  do { \
    Serial.printf((fmt), ##__VA_ARGS__); \
  } while (0)
#else
#define LOGE(...) do { } while (0)
#endif
