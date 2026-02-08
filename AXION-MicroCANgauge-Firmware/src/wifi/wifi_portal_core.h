#pragma once

#include <Arduino.h>

void WifiPortalCoreEnter();
void WifiPortalCoreExit();
void WifiPortalCoreTick(uint32_t now_ms);
class DiagDnsServer;
void WifiPortalCoreStartDns(DiagDnsServer& dns, IPAddress ip);
void WifiPortalCoreStopDns(DiagDnsServer& dns);

