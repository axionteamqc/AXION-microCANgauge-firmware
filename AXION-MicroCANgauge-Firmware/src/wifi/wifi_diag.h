#pragma once

#include <Arduino.h>

#ifndef WIFI_DIAG
#define WIFI_DIAG 0
#endif

#if WIFI_DIAG
void WifiDiagInit();
void WifiDiagTick(uint32_t now_ms);
void WifiDiagIncHttp();
void WifiDiagIncDns();
#else
inline void WifiDiagInit() {}
inline void WifiDiagTick(uint32_t) {}
inline void WifiDiagIncHttp() {}
inline void WifiDiagIncDns() {}
#endif
