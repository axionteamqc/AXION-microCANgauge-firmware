#include "wifi/wifi_portal_core.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <lwip/ip4_addr.h>
#include "wifi/diag_dns_server.h"
#if ARDUINO_USB_CDC_ON_BOOT && !defined(CONFIG_TINYUSB_CDC_ENABLED)
#define Serial Serial0
#endif

#include "app/app_sleep.h"
#include "config/logging.h"
#include "wifi/wifi_portal.h"

#ifndef WIFI_AP_OPEN_TEST
#define WIFI_AP_OPEN_TEST 0
#endif
#ifndef WIFI_ALLOW_OPEN_FALLBACK
#define WIFI_ALLOW_OPEN_FALLBACK 0
#endif

// AP lifecycle helpers (logic still lives in wifi_portal.cpp for now).
void WifiPortalCoreEnter() {
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
  const bool ap_cfg_ok_pre = WiFi.softAPConfig(ip, gw, mask);
  if (!ap_cfg_ok_pre) {
    LOGE("Wi-Fi AP config (pre) failed\r\n");
  }
#if WIFI_AP_OPEN_TEST
  bool ok = WiFi.softAP(WifiPortalSsid());
  if (!ok) {
    LOGW("Wi-Fi AP OPEN-TEST start failed, retrying WPA...\r\n");
    ok = WiFi.softAP(WifiPortalSsid(), WifiPortalPass(), 1, 0, 4);
    if (!ok) {
      LOGE("Wi-Fi AP start failed (both open+WPA)\r\n");
      return;
    }
  }
#else
  WifiPortalEnsureValidPass();
  const char* pass = WifiPortalPass();
  bool ok = WiFi.softAP(WifiPortalSsid(), pass, 1, 0, 4);
  if (!ok) {
    LOGE("Wi-Fi AP start failed\r\n");
#if WIFI_ALLOW_OPEN_FALLBACK
    ok = WiFi.softAP(WifiPortalSsid());
    if (!ok) {
      LOGE("Wi-Fi AP fallback failed\r\n");
      return;
    }
#else
    LOGW("Wi-Fi AP open fallback disabled\r\n");
    return;
#endif
  }
#endif
  // Give the AP stack a moment to bring up netif before configuring DHCP/IP.
  AppSleepMs(50);
  const bool ap_cfg_ok_post = WiFi.softAPConfig(ip, gw, mask);
  if (!ap_cfg_ok_post) {
    LOGE("Wi-Fi AP config (post) failed\r\n");
  }
  uint8_t ch = 0;
  wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&ch, &sec);
  LOGI(
      "Wi-Fi AP started%s: SSID=%s IP=%s channel=%u txpwr=%.1fdBm sta=%u\n",
      WIFI_AP_OPEN_TEST ? " (OPEN TEST)" : "", WifiPortalSsid(),
      WiFi.softAPIP().toString().c_str(), ch,
      static_cast<double>(WiFi.getTxPower()), WiFi.softAPgetStationNum());

  esp_netif_t* ap = nullptr;
  for (int i = 0; i < 10 && !ap; ++i) {
    ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap) {
      AppSleepMs(50);
    }
  }
  if (ap) {
    esp_netif_ip_info_t info{};
    IP4_ADDR(&info.ip, ip[0], ip[1], ip[2], ip[3]);
    IP4_ADDR(&info.gw, gw[0], gw[1], gw[2], gw[3]);
    IP4_ADDR(&info.netmask, mask[0], mask[1], mask[2], mask[3]);
    esp_netif_dhcps_stop(ap);
    const esp_err_t set_err = esp_netif_set_ip_info(ap, &info);
    if (set_err != ESP_OK) {
      LOGE("Wi-Fi AP set ip failed (%d)\r\n", static_cast<int>(set_err));
    }
    const esp_err_t start_err = esp_netif_dhcps_start(ap);
    if (start_err != ESP_OK) {
      LOGE("Wi-Fi AP dhcps start failed (%d)\r\n", static_cast<int>(start_err));
    } else {
      LOGI("Wi-Fi AP dhcps started\r\n");
    }
    esp_netif_dhcp_status_t st;
    if (esp_netif_dhcps_get_status(ap, &st) == ESP_OK) {
      const char* s = (st == ESP_NETIF_DHCP_STARTED)
                          ? "RUNNING"
                          : (st == ESP_NETIF_DHCP_STOPPED ? "STOPPED" : "UNKNOWN");
      LOGI("Wi-Fi AP dhcps=%s\n", s);
      (void)s;
    } else {
      LOGW("Wi-Fi AP dhcps status query failed\r\n");
    }
  } else {
    LOGW("Wi-Fi AP dhcps status unavailable\r\n");
  }
}

void WifiPortalCoreExit() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void WifiPortalCoreTick(uint32_t now_ms) {
  (void)now_ms;
  // No-op for now; kept for future expansion.
}
void WifiPortalCoreStartDns(DiagDnsServer& dns, IPAddress ip) {
  const String domain = "*";
  if (!dns.start(53, domain, ip)) {
    LOGE("[DNS] start failed\r\n");
    return;
  }
  dns.setErrorReplyCode(DiagDnsReplyCode::kNoError);
  LOGI("[DNS] start domain=%s reply=%d reqs=%lu ip=%s\n",
       domain.c_str(), static_cast<int>(DiagDnsReplyCode::kNoError),
       static_cast<unsigned long>(dns.request_count()),
       ip.toString().c_str());
}

void WifiPortalCoreStopDns(DiagDnsServer& dns) { dns.stop(); }

