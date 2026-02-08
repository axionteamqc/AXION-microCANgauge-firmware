#pragma once

#include <stdint.h>

bool WifiPortalMdnsStart(const char* host, uint16_t port_http);
void WifiPortalMdnsStop();
