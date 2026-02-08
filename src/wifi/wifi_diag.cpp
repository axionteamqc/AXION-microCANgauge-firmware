#include "wifi/wifi_diag.h"

#if WIFI_DIAG

#include <WiFi.h>
#include <type_traits>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_netif.h>
#include <esp_arduino_version.h>

#include "app/app_globals.h"
#include "app/app_ui_snapshot.h"
#include "config/logging.h"
#include "wifi/wifi_portal.h"
namespace {

uint32_t g_last_diag_ms = 0;
uint32_t g_http_count = 0;
uint32_t g_dns_count = 0;

template <typename T>
auto HasReasonMember(int) -> decltype(std::declval<T>().reason, std::true_type{}) {
  return {};
}
template <typename>
std::false_type HasReasonMember(...) {
  return {};
}
constexpr bool kApDiscHasReason =
    decltype(HasReasonMember<wifi_event_ap_stadisconnected_t>(0))::value;

const char* ResetReasonString(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_UNKNOWN: return "UNKNOWN";
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
  }
}

const char* WifiEventName(arduino_event_id_t ev) {
  switch (ev) {
    case ARDUINO_EVENT_WIFI_AP_START: return "AP_START";
    case ARDUINO_EVENT_WIFI_AP_STOP: return "AP_STOP";
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: return "AP_STA_CONNECTED";
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: return "AP_STA_DISCONNECTED";
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: return "AP_STA_IPASSIGNED";
    default: return "OTHER";
  }
}

[[maybe_unused]] const char* WifiDiscReasonStr(int reason) {
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
    case WIFI_REASON_NOT_AUTHED: return "NOT_AUTHED";
    case WIFI_REASON_NOT_ASSOCED: return "NOT_ASSOCED";
    case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
    case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "PWRCAP_BAD";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "SUPCHAN_BAD";
    case WIFI_REASON_IE_INVALID: return "IE_INVALID";
    case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_TIMEOUT";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GTK_TIMEOUT";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE4WAY_DIFFERS";
    case WIFI_REASON_GROUP_CIPHER_INVALID: return "GROUP_CIPHER_INVALID";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "PAIRWISE_CIPHER_INVALID";
    case WIFI_REASON_AKMP_INVALID: return "AKMP_INVALID";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "RSN_VER_UNSUPP";
    case WIFI_REASON_INVALID_RSN_IE_CAP: return "RSN_CAP_INVALID";
    case WIFI_REASON_802_1X_AUTH_FAILED: return "8021X_AUTH_FAILED";
    case WIFI_REASON_CIPHER_SUITE_REJECTED: return "CIPHER_REJECTED";
    case WIFI_REASON_INVALID_PMKID: return "INVALID_PMKID";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HS_TIMEOUT";
    default: return "OTHER";
  }
}

template <bool HasReason, typename T>
typename std::enable_if<HasReason, void>::type LogApDisc(
    const T& e, unsigned long now) {
  if (!kEnableVerboseSerialLogs) return;
  const int reason = static_cast<int>(e.reason);
  LOGI(
      "[WIFI_EVT] t=%lu %s mac=%02X:%02X:%02X:%02X:%02X:%02X aid=%u "
      "reason=%d %s\n",
      now, WifiEventName(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED), e.mac[0],
      e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5], e.aid, reason,
      WifiDiscReasonStr(reason));
}

template <bool HasReason, typename T>
typename std::enable_if<!HasReason, void>::type LogApDisc(
    const T& e, unsigned long now) {
  if (!kEnableVerboseSerialLogs) return;
  LOGI(
      "[WIFI_EVT] t=%lu %s mac=%02X:%02X:%02X:%02X:%02X:%02X aid=%u "
      "(no reason in struct, size=%u)\n",
      now, WifiEventName(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED), e.mac[0],
      e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5], e.aid,
      static_cast<unsigned>(sizeof(e)));
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&e);
  LOGI("[WIFI_EVT] ap_disc raw:");
  for (size_t i = 0; i < sizeof(e); ++i) {
    LOGI(" %02X", b[i]);
  }
  LOGI("\r\n");
}

}  // namespace

extern uint32_t dns_req_total;
extern uint32_t dns_max_us;

void WifiDiagIncHttp() { ++g_http_count; }
void WifiDiagIncDns() { ++g_dns_count; }

void WifiDiagInit() {
  if (!kEnableVerboseSerialLogs) {
    return;
  }
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  LOGI(
      "[WIFI_DIAG] chip rev=%u cores=%u features=0x%X sdk=%s cpu=%uMHz flash=%uMB\n",
      chip.revision, chip.cores, chip.features, ESP.getSdkVersion(),
      ESP.getCpuFreqMHz(), ESP.getFlashChipSize() / (1024 * 1024));
  LOGI("[WIFI_DIAG] idf=%s arduino=%s (%d.%d.%d)\n", esp_get_idf_version(),
#ifdef ESP_ARDUINO_VERSION_STRING
       ESP_ARDUINO_VERSION_STRING, ESP_ARDUINO_VERSION_MAJOR,
       ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH
#else
       "unknown", -1, -1, -1
#endif
  );
  const esp_reset_reason_t rr = esp_reset_reason();
  LOGI("[WIFI_DIAG] reset=%s (%d)\n", ResetReasonString(rr), rr);
  LOGI("[WIFI_DIAG] heap free=%u min=%u maxblk=%u\n", ESP.getFreeHeap(),
       ESP.getMinFreeHeap(),
       heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap) {
    esp_netif_dhcp_status_t st;
    if (esp_netif_dhcps_get_status(ap, &st) == ESP_OK) {
      const char* s = (st == ESP_NETIF_DHCP_STOPPED)
                          ? "STOPPED"
                          : (st == ESP_NETIF_DHCP_STARTED ? "RUNNING" : "UNKNOWN");
      LOGI("[WIFI_DIAG] dhcps=%s\n", s);
    } else {
      LOGW("[WIFI_DIAG] dhcps status query failed\r\n");
    }
  } else {
    LOGW("[WIFI_DIAG] dhcps status unavailable (no AP netif)\r\n");
  }

  WiFi.onEvent([](arduino_event_id_t ev, arduino_event_info_t info) {
    if (!kEnableVerboseSerialLogs) return;
    const uint32_t now = millis();
    switch (ev) {
      case ARDUINO_EVENT_WIFI_AP_START:
      case ARDUINO_EVENT_WIFI_AP_STOP: {
        LOGI("[WIFI_EVT] t=%lu %s\n", static_cast<unsigned long>(now),
             WifiEventName(ev));
        break;
      }
      case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
        const auto& e = info.wifi_ap_staconnected;
        LOGI(
            "[WIFI_EVT] t=%lu %s mac=%02X:%02X:%02X:%02X:%02X:%02X aid=%u\n",
            static_cast<unsigned long>(now), WifiEventName(ev), e.mac[0],
            e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5], e.aid);
        break;
      }
      case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: {
        const auto& e = info.wifi_ap_staipassigned;
        IPAddress ip(e.ip.addr);
        LOGI("[WIFI_EVT] t=%lu %s ip=%s\n", static_cast<unsigned long>(now),
             WifiEventName(ev), ip.toString().c_str());
        break;
      }
      case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
        const auto& e = info.wifi_ap_stadisconnected;
        LogApDisc<kApDiscHasReason>(e, static_cast<unsigned long>(now));
        break;
      }
      default:
        break;
    }
  });
}

void WifiDiagTick(uint32_t now_ms) {
  if (!kEnableVerboseSerialLogs) {
    return;
  }
  AppUiSnapshot ui;
  GetAppUiSnapshot(ui);
  if (!ui.wifi_mode_active) return;
  if ((now_ms - g_last_diag_ms) < 2000U) return;
  g_last_diag_ms = now_ms;

  LOGI(
      "[WIFI_DIAG] up=%lus heap=%u min=%u maxblk=%u mode=%d status=%d ap=%s "
      "sta=%u http=%lu dns=%lu dns_req=%lu dns_max_us=%lu root_max_ms=%lu root_min_blk=%lu\n",
      static_cast<unsigned long>(now_ms / 1000UL), ESP.getFreeHeap(),
      ESP.getMinFreeHeap(),
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), WiFi.getMode(),
      static_cast<int>(WiFi.status()), WiFi.softAPIP().toString().c_str(),
      WiFi.softAPgetStationNum(), g_http_count, g_dns_count, dns_req_total,
      dns_max_us, WifiPortalRootRenderMaxMs(), WifiPortalRootMinMaxBlk());

  if (WiFi.softAPgetStationNum() > 0) {
    esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap) {
      esp_netif_dhcp_status_t st;
      if (esp_netif_dhcps_get_status(ap, &st) == ESP_OK) {
        const char* s = (st == ESP_NETIF_DHCP_STARTED)
                            ? "RUNNING"
                            : (st == ESP_NETIF_DHCP_STOPPED ? "STOPPED" : "UNKNOWN");
        LOGI("[WIFI_DIAG] dhcps=%s sta=%u\n", s,
             WiFi.softAPgetStationNum());
      } else {
        LOGW("[WIFI_DIAG] dhcps status query failed (tick)\r\n");
      }
    } else {
      LOGW("[WIFI_DIAG] dhcps status unavailable (tick)\r\n");
    }
  }
}

#endif  // WIFI_DIAG
