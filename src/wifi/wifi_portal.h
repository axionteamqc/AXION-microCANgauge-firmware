#pragma once

#include <Arduino.h>
#include <IPAddress.h>

void WifiPortalEnter();
void WifiPortalExit();
void WifiPortalTick();
bool WifiPortalStarted();
const char* WifiPortalSsid();
const char* WifiPortalPass();
void WifiPortalEnsureValidPass();
IPAddress WifiPortalIp();
uint32_t WifiPortalDnsCount();
uint32_t WifiPortalDnsMaxUs();
uint32_t WifiPortalRootRenderMaxMs();
uint32_t WifiPortalRootMinMaxBlk();
