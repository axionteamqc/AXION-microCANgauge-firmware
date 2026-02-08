#pragma once

#include <stdint.h>

class WebServer;

void WifiPortalSseBegin(WebServer& server);
void WifiPortalSseTick(uint32_t now_ms);
void WifiPortalSseStop();
