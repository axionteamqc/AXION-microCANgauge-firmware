#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif.h>

#include "app/app_sleep.h"
#include "config/logging.h"
#include "wifi/diag_dns_server.h"
#include "wifi/wifi_diag.h"
#include "wifi/wifi_portal_http.h"

#ifndef WIFI_AP_OPEN_TEST
#define WIFI_AP_OPEN_TEST 0
#endif

#ifndef WIFI_PORTAL_DIAG_ONLY
#define WIFI_PORTAL_DIAG_ONLY 0
#endif

#if WIFI_PORTAL_DIAG_ONLY

namespace {
constexpr const char* kDiagSsid = "AXION-MCG";
constexpr const char* kDiagPass = "AMCG1234";

WebServer g_diag_server(80);
DiagDnsServer g_diag_dns;
uint32_t g_http_count = 0;
uint32_t g_dns_count_local = 0;

void logHttp() {
  ++g_http_count;
  if (kEnableVerboseSerialLogs) {
    LOGI("[DIAG_HTTP] %s %s from=%s len=%d t=%lu\n",
         g_diag_server.method() == HTTP_GET ? "GET" : "OTHER",
         g_diag_server.uri().c_str(),
         g_diag_server.client().remoteIP().toString().c_str(),
         g_diag_server.hasHeader("Content-Length")
             ? g_diag_server.header("Content-Length").toInt()
             : -1,
         static_cast<unsigned long>(millis()));
  }
}

void handleRoot() {
  logHttp();
  g_diag_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_diag_server.send(200, "text/html", "");
  PortalWriter send(g_diag_server);
  send.SendRaw("<html><body><h1>AXION Diag</h1><p>Diag-only firmware.</p>"
               "<p><a href=\"/diag\">/diag</a></p></body></html>");
  send.Flush();
  g_diag_server.sendContent("");
}

void handleDiag() {
  logHttp();
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  esp_netif_dhcp_status_t st;
  const char* dhcp = "unavail";
  if (ap && esp_netif_dhcps_get_status(ap, &st) == ESP_OK) {
    dhcp = (st == ESP_NETIF_DHCP_STARTED) ? "RUNNING"
                                          : (st == ESP_NETIF_DHCP_STOPPED ? "STOPPED" : "UNKNOWN");
  }
  uint8_t ch = 0;
  wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&ch, &sec);
  g_diag_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_diag_server.send(200, "application/json", "");
  PortalWriter send(g_diag_server);
  send.SendRaw("{\"heap_free\":");
  send.SendFmt("%u", static_cast<unsigned>(ESP.getFreeHeap()));
  send.SendRaw(",\"heap_min\":");
  send.SendFmt("%u", static_cast<unsigned>(ESP.getMinFreeHeap()));
  send.SendRaw(",\"station\":");
  send.SendFmt("%u", static_cast<unsigned>(WiFi.softAPgetStationNum()));
  send.SendRaw(",\"ip\":\"");
  IPAddress ip = WiFi.softAPIP();
  send.SendFmt("%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  send.SendRaw("\",\"dhcps\":\"");
  send.SendRaw(dhcp);
  send.SendRaw("\",\"dns_req\":");
  send.SendFmt("%lu", static_cast<unsigned long>(g_diag_dns.request_count()));
  send.SendRaw(",\"http_req\":");
  send.SendFmt("%lu", static_cast<unsigned long>(g_http_count));
  send.SendRaw(",\"channel\":");
  send.SendFmt("%u", static_cast<unsigned>(ch));
  send.SendRaw("}");
  send.Flush();
  g_diag_server.sendContent("");
}

}  // namespace

void DiagOnlySetup() {
  // Serial already started in app_boot.
  if (kEnableVerboseSerialLogs) {
    LOGI("FW DIAG BUILD: %s %s\r\n", __DATE__, __TIME__);
  }
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  AppSleepMs(100);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  IPAddress ip(192, 168, 4, 1);
  IPAddress gw(192, 168, 4, 1);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gw, mask);
  bool ok = false;
#if WIFI_AP_OPEN_TEST
  ok = WiFi.softAP(kDiagSsid);
  if (!ok) {
    LOGW("DIAG AP open test failed, retry WPA...\r\n");
    ok = WiFi.softAP(kDiagSsid, kDiagPass, 1, 0, 4);
  }
#else
  ok = WiFi.softAP(kDiagSsid, kDiagPass, 1, 0, 4);
#endif
  if (!ok) {
    LOGE("DIAG AP start failed\r\n");
    return;
  }
  g_diag_dns.start(53, "*", ip);

  g_diag_server.on("/", handleRoot);
  g_diag_server.on("/diag", handleDiag);
  g_diag_server.onNotFound([]() {
    logHttp();
    g_diag_server.sendHeader("Location", "/");
    g_diag_server.send(302, "text/plain", "");
  });
  g_diag_server.begin();
  WifiDiagInit();
  if (kEnableVerboseSerialLogs) {
    LOGI("DIAG AP%s: SSID=%s IP=%s\n",
         WIFI_AP_OPEN_TEST ? " (OPEN TEST)" : "", kDiagSsid,
         WiFi.softAPIP().toString().c_str());
  }
}

void DiagOnlyLoop() {
  g_diag_dns.processNextRequest();
  g_diag_server.handleClient();
  WifiDiagTick(millis());
  AppSleepMs(1);
}

#endif  // WIFI_PORTAL_DIAG_ONLY
