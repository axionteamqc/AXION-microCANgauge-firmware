#pragma once

#include <stddef.h>

#include "app_state.h"
#include "wifi/wifi_portal_fields.h"

class String;
class WebServer;

struct ApplyCommitData {
  DisplayTopology topo = DisplayTopology::kSmallOnly;
  bool flip0 = false;
  bool flip1 = false;
  bool oil_swap = false;
  UserSensorCfg user_sensor[2] = {};
  float stoich_afr = 14.7f;
  bool afr_show_lambda = false;
  bool demo_mode = false;
  bool ecu_type_valid = false;
  char ecu_type[8] = "";
  bool wifi_ap_pw_valid = false;
  char wifi_ap_pw[17] = "";
  uint8_t boot_pages_internal[kMaxZones] = {0, 0, 0};
  uint32_t can_bitrate_value = 0;
  bool can_bitrate_locked = false;
  bool can_ready = false;
  uint8_t id_present_mask = 0;
  uint32_t page_units_mask = 0;
  uint32_t page_alert_max_mask = 0;
  uint32_t page_alert_min_mask = 0;
  uint32_t page_hidden_mask = 0;
  Thresholds thresholds[kPageCount] = {};
};

bool parseInt(const String& s, long& out);
bool isPrintableAscii(const String& s);
bool parseIntArg(WebServer& server, const char* name, long& out);
bool parseCheckboxArg(WebServer& server, const char* name);
bool parseFloatArg(WebServer& server, const char* name, float& out);
bool validatePageIndex(long v, size_t page_count);

bool ParseBootPages(WebServer& server, size_t page_count,
                    uint8_t (&boot_pages_internal)[kMaxZones],
                    bool& warned_boot_page);

bool ApplyCommitAndPersist(const ApplyCommitData& data,
                           const String& brand_str,
                           const String& hello1_str,
                           const String& hello2_str);

void HandleApplyResetExtrema(WebServer& server, uint32_t& form_nonce);
void HandleApplyFactoryReset(WebServer& server, uint32_t& form_nonce);
void HandleApplyResetBaro(WebServer& server, uint32_t& form_nonce);
void SendApplySuccessResponse(WebServer& server,
                              const uint8_t boot_pages_internal[kMaxZones]);
