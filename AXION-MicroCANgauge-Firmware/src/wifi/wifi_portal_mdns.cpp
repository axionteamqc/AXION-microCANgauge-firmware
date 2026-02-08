#include "wifi/wifi_portal_mdns.h"

#include <ESPmDNS.h>

#include "config/logging.h"

namespace {

bool s_mdns_started = false;

}  // namespace

bool WifiPortalMdnsStart(const char* host, uint16_t port_http) {
  if (s_mdns_started) return true;
  if (host == nullptr || *host == '\0') return false;
  if (!MDNS.begin(host)) {
    LOGW("mDNS start failed (%s)\r\n", host);
    return false;
  }
  MDNS.addService("http", "tcp", port_http);
  MDNS.setInstanceName("AXION MicroCANgauge");
  s_mdns_started = true;
  LOGI("mDNS started: %s.local\r\n", host);
  return true;
}

void WifiPortalMdnsStop() {
  if (!s_mdns_started) return;
  MDNS.end();
  s_mdns_started = false;
}
