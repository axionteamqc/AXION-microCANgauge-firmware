#include "wifi/wifi_portal.h"
#include "wifi/wifi_portal_core.h"
#include "wifi/wifi_portal_http.h"

#include <WebServer.h>
#include <WiFi.h>
#include <functional>
#include "app_state.h"
#include "boot/boot_strings.h"
#include "config/factory_config.h"
#include "config/logging.h"
#include "settings/ui_persist_build.h"
#include "ui/pages.h"
#include "ecu/ecu_manager.h"
#include "app/app_globals.h"
#include "app/app_runtime.h"
#include "user_sensors/user_sensors.h"
#include <esp_heap_caps.h>
#include "wifi/diag_dns_server.h"
#include "wifi/wifi_diag.h"
#include "wifi/wifi_ap_pass.h"
#include "wifi/wifi_portal_mdns.h"
#include "wifi/wifi_portal_sse.h"
#include "wifi/wifi_portal_internal.h"
#include "wifi/wifi_portal_units.h"
#include "data/datastore.h"
#include "wifi/wifi_pass_validate.h"

namespace {


// moved to wifi_portal_http.cpp

bool started = false;
WebServer server(80);
DiagDnsServer dns;
uint32_t form_nonce = 0;

struct PortalContext {
  WebServer& server;
  DiagDnsServer& dns;
  bool& started;
  uint32_t& form_nonce;
  const char* ssid;
  const char* pass;
};

PortalContext& Ctx() {
  static PortalContext ctx{server, dns, started, form_nonce, WifiApSsid(),
                           nullptr};
  return ctx;
}

}  // namespace

WebServer& WifiPortalServer() { return server; }
uint32_t& WifiPortalFormNonce() { return form_nonce; }

using SendFn = PortalWriter;





uint32_t dns_req_total = 0;
uint32_t dns_max_us = 0;
uint32_t root_render_max_ms = 0;
uint32_t root_min_maxblk = 0;
volatile bool wifi_ota_in_progress = false;

const char* unitsForKind(ValueKind kind, bool imperial) {
  switch (kind) {
    case ValueKind::kPressure:
    case ValueKind::kBoost:
      return imperial ? "psi" : "kPa";
    case ValueKind::kTemp:
      return imperial ? "F" : "C";
    case ValueKind::kSpeed:
      return imperial ? "mph" : "km/h";
    case ValueKind::kVoltage:
      return "V";
    case ValueKind::kPercent:
      return "%";
    case ValueKind::kDeg:
      return "deg";
    case ValueKind::kRpm:
      return "rpm";
    case ValueKind::kAfr:
      return "AFR";
    default:
      return "";
  }
}

void WifiPortalEnter() {
  if (Ctx().started) return;
  WifiPortalCoreEnter();
  IPAddress ip = WiFi.softAPIP();
  WifiPortalCoreStartDns(dns, ip);
  WifiPortalHttpRegisterRoutes(server);
  server.begin();
  WifiPortalMdnsStart("axion", 80);
  Ctx().form_nonce = static_cast<uint32_t>(millis() ^ random(0xFFFFFFFF));
  Ctx().started = true;
}

void WifiPortalExit() {
  if (!Ctx().started) return;
  server.stop();
  WifiPortalMdnsStop();
  WifiPortalCoreStopDns(dns);
  WifiPortalSseStop();
  WifiPortalCoreExit();
  Ctx().started = false;
}

void WifiPortalTick() {
  if (!started) return;
  const uint32_t t0 = micros();
  dns.processNextRequest();
  const uint32_t dt = micros() - t0;
  ++dns_req_total;
  if (dt > dns_max_us) dns_max_us = dt;
  server.handleClient();
  WifiPortalSseTick(millis());
}

bool WifiPortalStarted() { return started; }

const char* WifiPortalSsid() { return WifiApSsid(); }

const char* WifiPortalPass() {
  return WifiApPass();
}

// Ensure we never attempt an invalid-length AP password.
void WifiPortalEnsureValidPass() {
  const char* pass = WifiApPass();
  if (!WifiApPassValid(pass)) {
    if (kEnableVerboseSerialLogs) {
      LOGW("Wi-Fi AP pass invalid, reverting to default\r\n");
    }
    WifiApPassResetToDefault();
  }
}

IPAddress WifiPortalIp() { return WiFi.softAPIP(); }

uint32_t WifiPortalDnsCount() { return dns_req_total; }
uint32_t WifiPortalDnsMaxUs() { return dns_max_us; }
uint32_t WifiPortalRootRenderMaxMs() { return root_render_max_ms; }
uint32_t WifiPortalRootMinMaxBlk() { return root_min_maxblk; }
