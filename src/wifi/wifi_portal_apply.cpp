#include "wifi/wifi_portal_handlers.h"

#include <Arduino.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "app/app_globals.h"
#include "app/app_sleep.h"
#include "app/app_runtime.h"
#include "app_state.h"
#include "config/factory_config.h"
#include "config/logging.h"
#include "ecu/ecu_manager.h"
#include "ui/pages.h"
#include "user_sensors/user_sensors.h"
#include "wifi/wifi_ap_pass.h"
#include "wifi/wifi_diag.h"
#include "wifi/wifi_pass_validate.h"
#include "wifi/wifi_portal_apply_internal.h"
#include "wifi/wifi_portal_error.h"
#include "wifi/wifi_portal_http.h"
#include "wifi/wifi_portal_internal.h"

namespace {

bool TryParseEcuType(const String& s, char* out, size_t out_len) {
  if (!out || out_len == 0) return false;
  if (s.length() == 0) return false;
  if (strcasecmp(s.c_str(), "MEGASQUIRT") == 0) {
    strlcpy(out, "MS3", out_len);
    return true;
  }
  if (strncasecmp(s.c_str(), "MS3", 3) == 0) {
    strlcpy(out, "MS3", out_len);
    return true;
  }
  if (strcasecmp(s.c_str(), "GENERIC") == 0) {
    strlcpy(out, "GENERIC", out_len);
    return true;
  }
  return false;
}

#if CORE_DEBUG_LEVEL >= 4
const char* topoToStr(DisplayTopology t) {
  switch (t) {
    case DisplayTopology::kSmallOnly:
      return "1xSmall";
    case DisplayTopology::kDualSmall:
      return "2xSmall";
    case DisplayTopology::kLargeOnly:
      return "1xLarge";
    case DisplayTopology::kLargePlusSmall:
      return "S+L";
    case DisplayTopology::kUnconfigured:
    default:
      return "Unconfigured";
  }
}
#endif

}  // namespace

void handleApply() {
  WebServer& server = WifiPortalServer();
  uint32_t& form_nonce = WifiPortalFormNonce();
  // Per-request warning flags to avoid log spam on tampered inputs.
  bool warned_confirm = false;
  bool warned_nonce = false;
  bool warned_wifi_pw = false;
  bool warned_action = false;
  bool warned_topology = false;
  bool warned_bitrate = false;
  bool warned_user_sensor = false;
  bool warned_boot_page = false;
  bool warned_boot_text = false;
  bool warned_hide_all = false;
  bool warned_threshold = false;
  bool warned_ecu = false;
  bool wifi_ap_pw_valid = false;
  char wifi_ap_pw[17] = "";
#if WIFI_PORTAL_HEAP_DIAG
  const uint32_t heap_before = ESP.getFreeHeap();
  const uint32_t min_before = ESP.getMinFreeHeap();
  const uint32_t maxblk_before =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
  WifiDiagIncHttp();
  if (server.method() != HTTP_POST) {
    handleRedirect();
    return;
  }
  const bool has_action = server.hasArg("action");
  const String action = has_action ? server.arg("action") : "apply";
  if (kEnableVerboseSerialLogs) {
    LOGV("[APPLY] action=%s\r\n", has_action ? action.c_str() : "(none)");
  }
  if (!server.hasArg("confirm")) {
    if (!warned_confirm) {
      warned_confirm = true;
      LOGW("[APPLY] invalid: missing confirm\r\n");
    }
    sendErrorHtml(server, 400, "Apply error", "Confirmation required");
    return;
  }
  // Nonce check (best-effort).
  if (!server.hasArg("nonce") ||
      server.arg("nonce").toInt() != static_cast<long>(form_nonce)) {
    if (!warned_nonce) {
      warned_nonce = true;
      LOGW("[APPLY] invalid: nonce mismatch\r\n");
    }
    sendErrorHtml(server, 400, "Apply error", "Invalid nonce");
    return;
  }
  // Optional Wi-Fi AP password update
  if (server.hasArg("wifi_ap_pw")) {
    String pw = server.arg("wifi_ap_pw");
    pw.trim();
    if (pw.length() > 0) {
      if (!ValidateWifiApPass(pw.c_str())) {
        if (!warned_wifi_pw) {
          warned_wifi_pw = true;
          LOGW("[APPLY] invalid: wifi_ap_pw\r\n");
        }
        sendErrorHtml(server, 400, "Apply error",
                      "Invalid Wi-Fi password (8-16 printable chars)");
        return;
      }
      strlcpy(wifi_ap_pw, pw.c_str(), sizeof(wifi_ap_pw));
      wifi_ap_pw_valid = true;
    }
  }
  if (action == "reset_extrema") {
    HandleApplyResetExtrema(server, form_nonce);
    return;
  }
  if (action == "factory_reset") {
    HandleApplyFactoryReset(server, form_nonce);
    return;
  }
  if (action == "reset_baro") {
    HandleApplyResetBaro(server, form_nonce);
    return;
  }
  if (action != "apply") {
    if (!warned_action) {
      warned_action = true;
      LOGW("[APPLY] invalid: action='%s'\r\n", action.c_str());
    }
    sendErrorHtml(server, 400, "Apply error", "Invalid action");
    return;
  }
  // page_count/page_mask are based on table index order, not PageId.
  size_t page_count = 0;
  GetPageTable(page_count);
  const uint32_t page_mask =
      (page_count >= 32) ? 0xFFFFFFFFu : ((1u << page_count) - 1);

  long topo_val = 0;
  if (!parseIntArg(server, "display_topology", topo_val)) {
    if (!warned_topology) {
      warned_topology = true;
      LOGW("[APPLY] invalid: display_topology parse\r\n");
    }
    sendErrorHtml(server, 400, "Apply error", "Invalid topology");
    return;
  }
  if (topo_val < 1 || topo_val > 4) {
    if (!warned_topology) {
      warned_topology = true;
      LOGW("[APPLY] invalid: display_topology out of range (%ld)\r\n", topo_val);
    }
    sendErrorHtml(server, 400, "Apply error", "Topology out of range");
    return;
  }
  DisplayTopology topo = static_cast<DisplayTopology>(topo_val);
  if (kEnableVerboseSerialLogs) {
    LOGV("handleApply: display_topology=%ld (%s)\r\n", topo_val, topoToStr(topo));
  }

  long bitrate_sel = 0;
  if (!parseIntArg(server, "can_bitrate", bitrate_sel) || bitrate_sel <= 0) {
    if (!warned_bitrate) {
      warned_bitrate = true;
      LOGW("[APPLY] invalid: can_bitrate parse\r\n");
    }
    sendErrorHtml(server, 400, "Apply error", "Invalid CAN bitrate");
    return;
  }
  bool bitrate_allowed = false;
  uint8_t rate_count = 0;
  const uint32_t* rates = g_ecu_mgr.profile().scanBitrates(rate_count);
  for (uint8_t i = 0; i < rate_count; ++i) {
    if (static_cast<uint32_t>(bitrate_sel) == rates[i]) {
      bitrate_allowed = true;
      break;
    }
  }
  if (!bitrate_allowed) {
    if (!warned_bitrate) {
      warned_bitrate = true;
      LOGW("[APPLY] invalid: can_bitrate unsupported (%ld)\r\n", bitrate_sel);
    }
    sendErrorHtml(server, 400, "Apply error", "CAN bitrate not supported");
    return;
  }

  auto clampPreset = [](long v) -> UserSensorPreset {
    if (v < 0) v = 0;
    if (v > 4) v = 4;
    return static_cast<UserSensorPreset>(v);
  };
  auto clampSource = [](long v) -> UserSensorSource {
    return (v == 1) ? UserSensorSource::kSensor2 : UserSensorSource::kSensor1;
  };
  UserSensorCfg us_cfg[2] = {g_state.user_sensor[0], g_state.user_sensor[1]};
  auto parseUserSensor = [&](uint8_t idx) -> bool {
    char key[24];
    long preset_v = static_cast<long>(us_cfg[idx].preset);
    snprintf(key, sizeof(key), "us%u_preset", static_cast<unsigned>(idx));
    if (parseIntArg(server, key, preset_v)) {
      us_cfg[idx].preset = clampPreset(preset_v);
      us_cfg[idx].kind = presetToKind(us_cfg[idx].preset);
    }
    long src_v =
        static_cast<long>(us_cfg[idx].source == UserSensorSource::kSensor2 ? 1 : 0);
    snprintf(key, sizeof(key), "us%u_src", static_cast<unsigned>(idx));
    if (parseIntArg(server, key, src_v)) {
      us_cfg[idx].source = clampSource(src_v);
    }
    snprintf(key, sizeof(key), "us%u_lbl", static_cast<unsigned>(idx));
    if (server.hasArg(key)) {
      String lbl = server.arg(key);
      lbl.trim();
      if (lbl.length() > 7 || !isPrintableAscii(lbl)) {
        if (!warned_user_sensor) {
          warned_user_sensor = true;
          LOGW("[APPLY] invalid: user sensor label idx=%u\r\n",
               static_cast<unsigned>(idx));
        }
        sendErrorHtml(server, 400, "Apply error", "Invalid user sensor label");
        return false;
      }
      strlcpy(us_cfg[idx].label, lbl.c_str(), sizeof(us_cfg[idx].label));
    }
    float scale = us_cfg[idx].scale;
    snprintf(key, sizeof(key), "us%u_scale", static_cast<unsigned>(idx));
    if (parseFloatArg(server, key, scale)) {
      us_cfg[idx].scale = scale;
    }
    float offset = us_cfg[idx].offset;
    snprintf(key, sizeof(key), "us%u_offset", static_cast<unsigned>(idx));
    if (parseFloatArg(server, key, offset)) {
      us_cfg[idx].offset = offset;
    }
    snprintf(key, sizeof(key), "us%u_um", static_cast<unsigned>(idx));
    if (server.hasArg(key)) {
      String um = server.arg(key);
      um.trim();
      if (um.length() > 4 || !isPrintableAscii(um)) {
        if (!warned_user_sensor) {
          warned_user_sensor = true;
          LOGW("[APPLY] invalid: user sensor unit metric idx=%u\r\n",
               static_cast<unsigned>(idx));
        }
        sendErrorHtml(server, 400, "Apply error", "Invalid unit metric");
        return false;
      }
      strlcpy(us_cfg[idx].unit_metric, um.c_str(),
              sizeof(us_cfg[idx].unit_metric));
    }
    snprintf(key, sizeof(key), "us%u_ui", static_cast<unsigned>(idx));
    if (server.hasArg(key)) {
      String ui = server.arg(key);
      ui.trim();
      if (ui.length() > 4 || !isPrintableAscii(ui)) {
        if (!warned_user_sensor) {
          warned_user_sensor = true;
          LOGW("[APPLY] invalid: user sensor unit imperial idx=%u\r\n",
               static_cast<unsigned>(idx));
        }
        sendErrorHtml(server, 400, "Apply error", "Invalid unit imperial");
        return false;
      }
      strlcpy(us_cfg[idx].unit_imperial, ui.c_str(),
              sizeof(us_cfg[idx].unit_imperial));
    }
    if (us_cfg[idx].preset != UserSensorPreset::kCustomUnitless) {
      us_cfg[idx].unit_metric[0] = '\0';
      us_cfg[idx].unit_imperial[0] = '\0';
    }
    return true;
  };
  if (!parseUserSensor(0)) return;
  if (!parseUserSensor(1)) return;
  float stoich = g_state.stoich_afr;
  if (parseFloatArg(server, "stoich_afr", stoich)) {
    if (stoich < 10.0f) stoich = 10.0f;
    if (stoich > 25.0f) stoich = 25.0f;
  }
  const bool afr_show_lambda = parseCheckboxArg(server, "afr_show_lambda");

  String key;
  key.reserve(32);
  String arg;
  arg.reserve(32);

  // User-facing Zone0/1/2 map directly to internal zone indices (0..2).
  uint8_t boot_pages_internal[kMaxZones];
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    boot_pages_internal[z] = g_state.boot_page_index[z];
  }
  if (!ParseBootPages(server, page_count, boot_pages_internal, warned_boot_page)) {
    return;
  }

  const bool flip0 = parseCheckboxArg(server, "flip0");
  const bool flip1 = parseCheckboxArg(server, "flip1");
  const bool oil_swap_checkbox = parseCheckboxArg(server, "oil_swap");
  String brand_str = server.arg("boot_brand_text");
  brand_str.trim();
  String hello1_str = server.arg("hello_line1_text");
  hello1_str.trim();
  String hello2_str = server.arg("hello_line2_text");
  hello2_str.trim();
  if (brand_str.length() > 16 || !isPrintableAscii(brand_str)) {
    if (!warned_boot_text) {
      warned_boot_text = true;
      LOGW("[APPLY] invalid: boot_brand_text\r\n");
    }
    sendErrorHtml(server, 400, "Apply error", "Invalid boot_brand_text");
    return;
  }
  if (hello1_str.length() > 16 || !isPrintableAscii(hello1_str)) {
    if (!warned_boot_text) {
      warned_boot_text = true;
      LOGW("[APPLY] invalid: hello_line1_text\r\n");
    }
    sendErrorHtml(server, 400, "Apply error", "Invalid hello_line1_text");
    return;
  }
  if (hello2_str.length() > 16 || !isPrintableAscii(hello2_str)) {
    if (!warned_boot_text) {
      warned_boot_text = true;
      LOGW("[APPLY] invalid: hello_line2_text\r\n");
    }
    sendErrorHtml(server, 400, "Apply error", "Invalid hello_line2_text");
    return;
  }

  const uint32_t new_can_bitrate_value = static_cast<uint32_t>(bitrate_sel);
  const bool new_can_bitrate_locked = false;
  const bool new_can_ready = false;
  const uint8_t new_id_present_mask = 0;

  const bool demo_mode = server.hasArg("demo_mode");
  String ecu = server.arg("ecu_type");
  ecu.trim();
  // Rebuild masks from per-page fields
  uint32_t units_mask = 0;
  uint32_t alert_max_mask = 0;
  uint32_t alert_min_mask = 0;
  for (size_t i = 0; i < page_count; ++i) {
    bool imperial = false;
    char key[16];
    snprintf(key, sizeof(key), "units_%u", static_cast<unsigned>(i));
    if (server.hasArg(key)) {
      arg = server.arg(key);
      long v = arg.toInt();
      imperial = (v != 0);
    }
    if (imperial) units_mask |= (1U << i);
    snprintf(key, sizeof(key), "amax_%u", static_cast<unsigned>(i));
    if (server.hasArg(key)) {
      arg = server.arg(key);
      if (arg != "0") alert_max_mask |= (1U << i);
    }
    snprintf(key, sizeof(key), "amin_%u", static_cast<unsigned>(i));
    if (server.hasArg(key)) {
      arg = server.arg(key);
      if (arg != "0") alert_min_mask |= (1U << i);
    }
  }
  uint32_t hidden_mask = 0;
  for (size_t i = 0; i < page_count; ++i) {
    char key[16];
    snprintf(key, sizeof(key), "hide_%u", static_cast<unsigned>(i));
    if (server.hasArg(key)) {
      arg = server.arg(key);
      if (arg.length() > 0) hidden_mask |= static_cast<uint32_t>(1U << i);
    }
  }
  if (hidden_mask == page_mask) {
    if (!warned_hide_all) {
      warned_hide_all = true;
      LOGW("[APPLY] invalid: hide all pages\r\n");
    }
    sendErrorHtml(server, 400, "Apply error", "Cannot hide all pages");
    return;
  }
  // Thresholds (display -> canon). Empty -> NAN.
  Thresholds new_thresholds[kPageCount];
  String v;
  v.reserve(32);
  for (size_t i = 0; i < kPageCount; ++i) {
    const PageMeta* meta =
        (i < page_count) ? FindPageMeta(GetPageTable(page_count)[i].id) : nullptr;
    const ValueKind kind = meta ? meta->kind : ValueKind::kNone;
    ScreenSettings cfg{};
    cfg.imperial_units = (units_mask & (1U << i)) != 0;
    cfg.flip_180 = false;
    ThresholdGrid grid{};
    const bool has_grid = (i < page_count)
                              ? GetThresholdGrid(GetPageTable(page_count)[i].id,
                                                 cfg.imperial_units, grid)
                              : false;
    float new_min = NAN;
    float new_max = NAN;
    char key[24];
    snprintf(key, sizeof(key), "thr_min_%u", static_cast<unsigned>(i));
    if (server.hasArg(key)) {
      v = server.arg(key);
      v.trim();
      if (v.length() > 0) {
        char* endp = nullptr;
        float disp = strtof(v.c_str(), &endp);
        if (endp == v.c_str() || *endp != '\0' || isnan(disp) || isinf(disp)) {
          if (!warned_threshold) {
            warned_threshold = true;
            LOGW("[APPLY] invalid: thr_min parse page=%u\r\n",
                 static_cast<unsigned>(i));
          }
          sendErrorHtml(server, 400, "Apply error", "Invalid thr_min");
          return;
        }
        if (!has_grid || !IsOnGrid(disp, grid)) {
          if (!warned_threshold) {
            warned_threshold = true;
            LOGW("[APPLY] invalid: thr_min off-grid page=%u\r\n",
                 static_cast<unsigned>(i));
          }
          sendErrorHtml(server, 400, "Apply error", "Invalid thr_min (off-grid)");
          return;
        }
        new_min = DisplayToCanon(kind, disp, cfg);
      }
    }
    snprintf(key, sizeof(key), "thr_max_%u", static_cast<unsigned>(i));
    if (server.hasArg(key)) {
      v = server.arg(key);
      v.trim();
      if (v.length() > 0) {
        char* endp = nullptr;
        float disp = strtof(v.c_str(), &endp);
        if (endp == v.c_str() || *endp != '\0' || isnan(disp) || isinf(disp)) {
          if (!warned_threshold) {
            warned_threshold = true;
            LOGW("[APPLY] invalid: thr_max parse page=%u\r\n",
                 static_cast<unsigned>(i));
          }
          sendErrorHtml(server, 400, "Apply error", "Invalid thr_max");
          return;
        }
        if (!has_grid || !IsOnGrid(disp, grid)) {
          if (!warned_threshold) {
            warned_threshold = true;
            LOGW("[APPLY] invalid: thr_max off-grid page=%u\r\n",
                 static_cast<unsigned>(i));
          }
          sendErrorHtml(server, 400, "Apply error", "Invalid thr_max (off-grid)");
          return;
        }
        new_max = DisplayToCanon(kind, disp, cfg);
      }
    }
    if (!isnan(new_min) && !isnan(new_max) && new_min > new_max) {
      if (!warned_threshold) {
        warned_threshold = true;
        LOGW("[APPLY] invalid: thr_min > thr_max page=%u\r\n",
             static_cast<unsigned>(i));
      }
      sendErrorHtml(server, 400, "Apply error", "Threshold min > max");
      return;
    }
    new_thresholds[i].min = new_min;
    new_thresholds[i].max = new_max;
  }

  const uint32_t new_units_mask = units_mask & page_mask;
  const uint32_t new_alert_max_mask = alert_max_mask & page_mask;
  const uint32_t new_alert_min_mask = alert_min_mask & page_mask;

  ApplyCommitData commit{};
  commit.topo = topo;
  commit.flip0 = flip0;
  commit.flip1 = flip1;
  commit.oil_swap = oil_swap_checkbox;
  commit.user_sensor[0] = us_cfg[0];
  commit.user_sensor[1] = us_cfg[1];
  commit.stoich_afr = stoich;
  commit.afr_show_lambda = afr_show_lambda;
  commit.demo_mode = demo_mode;
  if (TryParseEcuType(ecu, commit.ecu_type, sizeof(commit.ecu_type))) {
    commit.ecu_type_valid = true;
  } else if (ecu.length() > 0) {
    if (!warned_ecu) {
      warned_ecu = true;
      LOGW("[APPLY] invalid: ecu_type='%s'\r\n", ecu.c_str());
    }
  }
  memcpy(commit.boot_pages_internal, boot_pages_internal,
         sizeof(commit.boot_pages_internal));
  commit.can_bitrate_value = new_can_bitrate_value;
  commit.can_bitrate_locked = new_can_bitrate_locked;
  commit.can_ready = new_can_ready;
  commit.id_present_mask = new_id_present_mask;
  commit.page_units_mask = new_units_mask;
  commit.page_alert_max_mask = new_alert_max_mask;
  commit.page_alert_min_mask = new_alert_min_mask;
  commit.page_hidden_mask = hidden_mask;
  memcpy(commit.thresholds, new_thresholds, sizeof(new_thresholds));
  if (wifi_ap_pw_valid) {
    commit.wifi_ap_pw_valid = true;
    strlcpy(commit.wifi_ap_pw, wifi_ap_pw, sizeof(commit.wifi_ap_pw));
  }

  const bool persist_ok =
      ApplyCommitAndPersist(commit, brand_str, hello1_str, hello2_str);
  if (!persist_ok) {
    sendErrorHtml(
        server, 500, "Persist failed",
        "Applied in RAM, NOT saved to NVS. No reboot performed. "
        "Retry, then reboot/power-cycle manually if needed.");
    return;
  }

  if (kEnableVerboseSerialLogs) {
    LOGV("[APPLY] boot parsed Z0=%u Z1=%u Z2=%u\r\n",
         static_cast<unsigned int>(boot_pages_internal[0]),
         static_cast<unsigned int>(boot_pages_internal[1]),
         static_cast<unsigned int>(boot_pages_internal[2]));
    LOGV("handleApply: args_count=%u\r\n", server.args());
    for (uint8_t i = 0; i < server.args(); ++i) {
      LOGV("  arg[%u] %s=%s\r\n", i, server.argName(i).c_str(),
           server.arg(i).c_str());
    }
    for (uint8_t z = 0; z < kMaxZones; ++z) {
      key = kFieldBootPage[z];
      LOGV("  has %s=%s value=%u\r\n", key.c_str(),
           server.hasArg(key) ? "yes" : "no",
           static_cast<unsigned int>(g_state.boot_page_index[z]));
    }
    LOGV("  final boot pages: Z0=%u Z1=%u Z2=%u\r\n",
         static_cast<unsigned int>(g_state.boot_page_index[0]),
         static_cast<unsigned int>(g_state.boot_page_index[1]),
         static_cast<unsigned int>(g_state.boot_page_index[2]));
    LOGV("  page_index before save: Z0=%u Z1=%u Z2=%u\r\n",
         static_cast<unsigned int>(g_state.page_index[0]),
         static_cast<unsigned int>(g_state.page_index[1]),
         static_cast<unsigned int>(g_state.page_index[2]));
  }

  // Manual test hint:
  // 1) Change Display Topology and Boot Page Zone 1/2 via Wi-Fi page.
  // 2) Click Config & Reboot -> should not show "Invalid boot page".

  SendApplySuccessResponse(server, boot_pages_internal);
#if WIFI_PORTAL_HEAP_DIAG
  const uint32_t heap_after = ESP.getFreeHeap();
  const uint32_t min_after = ESP.getMinFreeHeap();
  const uint32_t maxblk_after =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const int32_t heap_delta =
      static_cast<int32_t>(heap_after) - static_cast<int32_t>(heap_before);
  LOGD("[PORTAL_HEAP] /apply heap=%u->%u (d=%ld) min=%u->%u maxblk=%u->%u\n",
       static_cast<unsigned>(heap_before),
       static_cast<unsigned>(heap_after),
       static_cast<long>(heap_delta),
       static_cast<unsigned>(min_before),
       static_cast<unsigned>(min_after),
       static_cast<unsigned>(maxblk_before),
       static_cast<unsigned>(maxblk_after));
#endif
  AppSleepMs(100);
  ESP.restart();
}
