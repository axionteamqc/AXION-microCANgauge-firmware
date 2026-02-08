#pragma once

#include <stdint.h>

class WebServer;

WebServer& WifiPortalServer();
uint32_t& WifiPortalFormNonce();

extern uint32_t root_render_max_ms;
extern uint32_t root_min_maxblk;
extern volatile bool wifi_ota_in_progress;
