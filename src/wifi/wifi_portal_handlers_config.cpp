#include "wifi/wifi_portal_handlers.h"

#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "app/app_globals.h"
#include "app/app_sleep.h"
#include "app/app_ui_snapshot.h"
#include "app/app_runtime.h"
#include "app/i2c_oled_log.h"
#include "app_state.h"
#include "boot/boot_strings.h"
#include "config/factory_config.h"
#include "config/logging.h"
#include "ecu/ecu_manager.h"
#include "settings/ui_persist_build.h"
#include "ui/pages.h"
#include "user_sensors/user_sensors.h"
#include "wifi/wifi_ap_pass.h"
#include "wifi/wifi_diag.h"
#include "wifi/wifi_portal_error.h"
#include "wifi/wifi_portal_escape.h"
#include "wifi/wifi_portal_fields.h"
#include "wifi/wifi_pass_validate.h"
#include "wifi/wifi_portal_http.h"
#include "wifi/wifi_portal_internal.h"
#include "wifi/wifi_portal_render_config.h"
#include "wifi/wifi_portal_units.h"

void handleRoot() {
  WebServer& server = WifiPortalServer();
  uint32_t& form_nonce = WifiPortalFormNonce();
  const uint32_t t0_us = micros();
  const uint32_t heap_before = ESP.getFreeHeap();
  const uint32_t min_before = ESP.getMinFreeHeap();
  const uint32_t maxblk_before =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  size_t bytes_sent = 0;

  WifiDiagIncHttp();
  AppUiSnapshot ui;
  GetAppUiSnapshot(ui);
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html", "");

  SendFn send(server, &bytes_sent);
  renderHtmlHead(send);
  bool cfg_pending = false;
  g_nvs.loadCfgPending(cfg_pending);
  if (cfg_pending) {
    send("<div style='margin:10px 0;padding:10px;border:2px solid #b00;"
         "background:#fee;color:#b00;font-weight:700;font-size:1.05rem;'>"
         "Last settings were NOT saved (cfg_pending). Re-apply to persist "
         "or Factory Reset.</div>");
  }
  if (!ui.oled_primary_ready && !ui.oled_secondary_ready) {
    send("<div style='margin:10px 0;padding:10px;border:2px solid #b00;"
         "background:#fee;color:#b00;font-weight:700;font-size:1.1rem;'>"
         "No OLED detected. Check power and the I2C bus.</div>");
  }
  renderDownloads(send);
  renderSetupSnapshot(send, page_count, ui);
  if (!pages || page_count == 0) {
    send("<div class='warn'>No pages registered. "
         "Firmware page table is empty.</div>");
  }

  send("<h2>Config</h2>");
  send("<form method='POST' action='/apply'>");
  send.SendFmt("<input type='hidden' name='nonce' value='%lu'>",
               static_cast<unsigned long>(form_nonce));
  const uint8_t topo_val = ui.display_topology;
  send("<div>Display topology: <select name='display_topology'>");
  send("<option value='1'");
  if (topo_val == 1) send(" selected");
  send(">1xSmall</option>");
  send("<option value='2'");
  if (topo_val == 2) send(" selected");
  send(">2xSmall</option>");
  send("<option value='3'");
  if (topo_val == 3) send(" selected");
  send(">1xLarge</option>");
  send("<option value='4'");
  if (topo_val == 4) send(" selected");
  send(">Large+Small</option>");
  send("</select></div>");
  send("<div class='checklist'>");
  send("<div class='check-row'><input type='checkbox' name='flip0' value='1' ");
  if (ui.screen_flip[0]) {
    send.SendRaw("checked");
  }
  send("><span>Screen 1 flip</span></div>");
  send("<div class='check-row'><input type='checkbox' name='flip1' value='1' ");
  if (ui.screen_flip[1]) {
    send.SendRaw("checked");
  }
  send("><span>Screen 2 flip</span></div>");
  const bool demo_checked =
      ui.demo_mode || (!AppConfig::IsRealCanEnabled());
  send("<div class='check-row'><input type='checkbox' name='demo_mode' id='demo_mode' value='1' ");
  if (demo_checked) {
    send.SendRaw("checked");
  }
  send("><span>Demo Mode</span></div>");
  const bool oil_swapped =
      (ui.user_sensor[0].source == UserSensorSource::kSensor2) &&
      (ui.user_sensor[1].source == UserSensorSource::kSensor1);
  send("<div class='check-row'><input type='checkbox' name='oil_swap' value='1' ");
  if (oil_swapped) {
    send.SendRaw("checked");
  }
  send("><span>Swap Sensor1 &#x21c4; Sensor2 (OilP/OilT)</span></div>");
  send("</div>");
  send("<details><summary>Wi-Fi</summary>"
       "<div style='margin-top:6px; margin-bottom:6px;'>"
       "<label for='wifi_ap_pw'>AP password</label>"
       "<input type='password' name='wifi_ap_pw' id='wifi_ap_pw' minlength='8' maxlength='16' value=''>"
       "</div>"
       "<p style='margin:4px 0 8px 0;'>8 &agrave; 16 caract&egrave;res. "
       "Apr&egrave;s Apply, reboot et reconnexion requise. Le MDP est affich&eacute; "
       "sur l&apos;&eacute;cran.</p>"
       "</details>");
  send("<h3>User Sensors</h3>");
  send("<div class='table-wrap'><table class='wide'>");
  send("<tr><th>#</th><th>Preset</th><th>Source</th><th>Label</th><th>Scale</th><th>Offset</th><th>Unit (metric)</th><th>Unit (imperial)</th></tr>");
  auto renderUsRow = [&](uint8_t idx, const UserSensorCfg& us) {
    send("<tr><td>");
    send.SendFmt("%u", static_cast<unsigned int>(idx + 1));
    send("</td><td><select name='us");
    send.SendFmt("%u", static_cast<unsigned int>(idx));
    send("_preset'>");
    struct Option {
      UserSensorPreset preset;
      const char* label;
    };
    static const Option kOpts[] = {
        {UserSensorPreset::kOilPressure, "Oil Pressure"},
        {UserSensorPreset::kOilTemp, "Oil Temp"},
        {UserSensorPreset::kFuelPressure, "Fuel Pressure"},
        {UserSensorPreset::kCustomVoltage, "Custom Voltage"},
        {UserSensorPreset::kCustomUnitless, "Custom Unitless"},
    };
    for (const auto& opt : kOpts) {
      send("<option value='");
      send.SendFmt("%u", static_cast<unsigned int>(opt.preset));
      send("'");
      if (opt.preset == us.preset) send(" selected");
      send(">");
      send(opt.label);
      send("</option>");
    }
    send("</select></td><td><select name='us");
    send.SendFmt("%u", static_cast<unsigned int>(idx));
    send("_src'><option value='0'");
    if (us.source == UserSensorSource::kSensor1) send(" selected");
    send(">Sensor1</option><option value='1'");
    if (us.source == UserSensorSource::kSensor2) send(" selected");
    send(">Sensor2</option></select></td><td>");
    send("<input type='text' name='us");
    send.SendFmt("%u", static_cast<unsigned int>(idx));
    send("_lbl' value='");
    SendHtmlEscaped(send, us.label);
    send("' maxlength='7'></td><td>");
    send("<input type='text' name='us");
    send.SendFmt("%u", static_cast<unsigned int>(idx));
    send("_scale' value='");
    send.SendFmt("%.3f", static_cast<double>(us.scale));
    send("'></td><td><input type='text' name='us");
    send.SendFmt("%u", static_cast<unsigned int>(idx));
    send("_offset' value='");
    send.SendFmt("%.3f", static_cast<double>(us.offset));
    send("'></td><td><input type='text' name='us");
    send.SendFmt("%u", static_cast<unsigned int>(idx));
    send("_um' value='");
    SendHtmlEscaped(send, us.unit_metric);
    send("' maxlength='4'></td><td><input type='text' name='us");
    send.SendFmt("%u", static_cast<unsigned int>(idx));
    send("_ui' value='");
    SendHtmlEscaped(send, us.unit_imperial);
    send("' maxlength='4'></td></tr>");
  };
  renderUsRow(0, ui.user_sensor[0]);
  renderUsRow(1, ui.user_sensor[1]);
  send("</table></div>");
  send("<h3>AFR / Lambda</h3>");
  send.SendFmt("<div class='check-row'><label>Stoich AFR</label>"
               "<input type='number' name='stoich_afr' step='0.1' min='10' max='25' value='%.1f'></div>",
               static_cast<double>(ui.stoich_afr));
  send("<div class='check-row'><input type='checkbox' name='afr_show_lambda' value='1' ");
  if (ui.afr_show_lambda) {
    send.SendRaw("checked");
  }
  send("><span>Display lambda instead of AFR</span></div>");
  auto zoneLabel = [&](uint8_t z, char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    switch (static_cast<DisplayTopology>(ui.display_topology)) {
      case DisplayTopology::kLargePlusSmall:
        if (z == 0) {
          strlcpy(out, "OLED1", out_len);
          return;
        }
        if (z == 1) {
          strlcpy(out, "OLED1", out_len);
          return;
        }
        if (z == 2) {
          strlcpy(out, "OLED2", out_len);
          return;
        }
        break;
      case DisplayTopology::kDualSmall:
        if (z == 0) {
          strlcpy(out, "OLED1", out_len);
          return;
        }
        if (z == 2) {
          strlcpy(out, "OLED2", out_len);
          return;
        }
        if (z == 1) {
          strlcpy(out, "Unused", out_len);
          return;
        }
        break;
      case DisplayTopology::kLargeOnly:
        if (z == 0) {
          strlcpy(out, "OLED1", out_len);
          return;
        }
        if (z == 1) {
          strlcpy(out, "OLED1", out_len);
          return;
        }
        break;
      case DisplayTopology::kSmallOnly:
        if (z == 0) {
          strlcpy(out, "OLED1", out_len);
          return;
        }
        break;
      case DisplayTopology::kUnconfigured:
      default:
        break;
    }
    snprintf(out, out_len, "Zone %u", static_cast<unsigned>(z));
  };
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    const char* field = kFieldBootPage[z];
    char zone_label[16];
    zoneLabel(z, zone_label, sizeof(zone_label));
    char label[64];
    snprintf(label, sizeof(label), "Boot Page %s", zone_label);
    char sel_id[24];
    snprintf(sel_id, sizeof(sel_id), "%s_sel", field);
    const char* hid_id = field;
    renderBootSelect(send, page_count, ui.boot_page_index[z], field, label,
                     sel_id, hid_id);
  }
  // Hidden fields already emitted by renderBootSelect; ensure sync on submit.
  send("<script>syncBootPages();</script>");
  renderBootTextInputs(send);
  renderPerPageTable(send, page_count, pages, ui);
  send("<details><summary>Advanced Options</summary>");
  send("<div>ECU Type: <select name='ecu_type'>");
  send("<option value='MS3'");
  if (strcmp(ui.ecu_type, "MS3") == 0) send(" selected");
  send(">Megasquirt</option>");
  send("<option value='GENERIC'");
  if (strcmp(ui.ecu_type, "GENERIC") == 0) send(" selected");
  send(">Generic</option>");
  send("</select></div>");
  {
    uint8_t rate_count = 0;
      const uint32_t* rates = g_ecu_mgr.profile().scanBitrates(rate_count);
    send("<div>CAN bitrate: <select name='can_bitrate'>");
    for (uint8_t i = 0; i < rate_count; ++i) {
      const uint32_t r = rates[i];
      char rate_buf[16];
      send("<option value='");
      snprintf(rate_buf, sizeof(rate_buf), "%lu", static_cast<unsigned long>(r));
      send(rate_buf);
      send("'");
      if (ui.can_bitrate_value == r) send(" selected");
      send(">");
      snprintf(rate_buf, sizeof(rate_buf), "%lu",
               static_cast<unsigned long>(r / 1000));
      send(rate_buf);
      send(" kbps");
      send("</option>");
    }
    send("</select>");
    send(" &nbsp; (lock: ");
    send(ui.can_bitrate_locked ? "ON" : "OFF");
    send(")</div>");
  }
  send("</details>");
  renderActions(send);
  send("</form>");

  renderRecordedExtrema(send, page_count, pages, ui);
  send("<a class='bigbtn' href='/live'>Live Data</a>");
  renderHtmlFooter(send);

  const uint32_t heap_after = ESP.getFreeHeap();
  const uint32_t min_after = ESP.getMinFreeHeap();
  const uint32_t maxblk_after =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const uint32_t dt_us = micros() - t0_us;
  const uint32_t dt_ms = dt_us / 1000U;
  const int32_t heap_delta =
      static_cast<int32_t>(heap_after) - static_cast<int32_t>(heap_before);
#if WIFI_PORTAL_HEAP_DIAG
  LOGD("[PORTAL_HEAP] / heap=%u->%u (d=%ld) min=%u->%u maxblk=%u->%u\n",
       static_cast<unsigned>(heap_before), static_cast<unsigned>(heap_after),
       static_cast<long>(heap_delta),
       static_cast<unsigned>(min_before), static_cast<unsigned>(min_after),
       static_cast<unsigned>(maxblk_before),
       static_cast<unsigned>(maxblk_after));
#endif
  if (!kEnableVerboseSerialLogs) {
    (void)min_before;
    (void)min_after;
    (void)heap_delta;
  }
  if (kEnableVerboseSerialLogs) {
    LOGI(
        "[HTTP_DIAG] / render dt=%lums bytes=%u heap=%u->%u (d=%ld) min=%u->%u "
        "maxblk=%u->%u\n",
        static_cast<unsigned long>(dt_ms), static_cast<unsigned>(bytes_sent),
        heap_before, heap_after, static_cast<long>(heap_delta), min_before,
        min_after, maxblk_before, maxblk_after);
  }
  if (dt_ms > root_render_max_ms) root_render_max_ms = dt_ms;
  const uint32_t min_blk =
      (maxblk_after < maxblk_before) ? maxblk_after : maxblk_before;
  if (root_min_maxblk == 0 || min_blk < root_min_maxblk) {
    root_min_maxblk = min_blk;
  }
  if (kEnableVerboseSerialLogs && (dt_ms > 500 || heap_after < 25000)) {
    LOGW("[HTTP_DIAG] WARN: slow render or low heap\r\n");
  }
}

void handleI2cOledLog() {
  WebServer& server = WifiPortalServer();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  PortalWriter send(server);
  renderHtmlHead(send);
  send("<h1>I2C / OLED Logs</h1>");

  AppUiSnapshot ui{};
  GetAppUiSnapshot(ui);
  send.SendFmt("<p>OLED1: %s | OLED2: %s</p>",
               ui.oled_primary_ready ? "ready" : "missing",
               ui.oled_secondary_ready ? "ready" : "missing");
  const uint32_t oled1_hz = g_oled_primary.busClockHz();
  const uint32_t oled2_hz = g_oled_secondary.busClockHz();
  send.SendFmt("<p>Bus clocks: OLED1 %lu Hz | OLED2 %lu Hz</p>",
               static_cast<unsigned long>(oled1_hz),
               static_cast<unsigned long>(oled2_hz));

  const uint16_t count = I2cOledLogCount();
  send.SendFmt("<p>RAM log entries: %u</p>", static_cast<unsigned>(count));

  I2cOledLogEntry entries[64];
  const uint16_t n = I2cOledLogCopy(entries, 64);
  send("<div class='table-wrap'><table><tr>"
       "<th>t(ms)</th><th>OLED</th><th>action</th><th>ok</th>"
       "<th>SDA</th><th>SCL</th></tr>");
  for (uint16_t i = 0; i < n; ++i) {
    const I2cOledLogEntry& e = entries[i];
    char sda_buf[8];
    char scl_buf[8];
    if (e.sda == 0xFF) {
      snprintf(sda_buf, sizeof(sda_buf), "?");
    } else {
      snprintf(sda_buf, sizeof(sda_buf), "%u", static_cast<unsigned>(e.sda));
    }
    if (e.scl == 0xFF) {
      snprintf(scl_buf, sizeof(scl_buf), "?");
    } else {
      snprintf(scl_buf, sizeof(scl_buf), "%u", static_cast<unsigned>(e.scl));
    }
    send("<tr><td>");
    send.SendFmt("%lu", static_cast<unsigned long>(e.ms));
    send("</td><td>");
    send.SendFmt("%u", static_cast<unsigned>(e.oled));
    send("</td><td>");
    send(I2cOledActionStr(static_cast<I2cOledAction>(e.action)));
    send("</td><td>");
    send(e.ok ? "ok" : "fail");
    send("</td><td>");
    send(sda_buf);
    send("</td><td>");
    send(scl_buf);
    send("</td></tr>");
  }
  send("</table></div>");

  I2cOledLogSnapshot snap{};
  if (I2cOledLogLoadSnapshot(snap)) {
    send("<h2>Saved Snapshot (NVS)</h2>");
    send.SendFmt("<p>Saved at %lu ms, entries: %u</p>",
                 static_cast<unsigned long>(snap.saved_ms),
                 static_cast<unsigned>(snap.count));
    send("<div class='table-wrap'><table><tr>"
         "<th>t(ms)</th><th>OLED</th><th>action</th><th>ok</th>"
         "<th>SDA</th><th>SCL</th></tr>");
    for (uint16_t i = 0; i < snap.count && i < 64; ++i) {
      const I2cOledLogEntry& e = snap.entries[i];
      char sda_buf[8];
      char scl_buf[8];
      if (e.sda == 0xFF) {
        snprintf(sda_buf, sizeof(sda_buf), "?");
      } else {
        snprintf(sda_buf, sizeof(sda_buf), "%u", static_cast<unsigned>(e.sda));
      }
      if (e.scl == 0xFF) {
        snprintf(scl_buf, sizeof(scl_buf), "?");
      } else {
        snprintf(scl_buf, sizeof(scl_buf), "%u", static_cast<unsigned>(e.scl));
      }
      send("<tr><td>");
      send.SendFmt("%lu", static_cast<unsigned long>(e.ms));
      send("</td><td>");
      send.SendFmt("%u", static_cast<unsigned>(e.oled));
      send("</td><td>");
      send(I2cOledActionStr(static_cast<I2cOledAction>(e.action)));
      send("</td><td>");
      send(e.ok ? "ok" : "fail");
      send("</td><td>");
      send(sda_buf);
      send("</td><td>");
      send(scl_buf);
      send("</td></tr>");
    }
    send("</table></div>");
  } else {
    send("<p>No snapshot saved in NVS.</p>");
  }

  send("<p><a href='/'>Back to portal</a></p>");
  renderHtmlFooter(send);
  server.sendContent("");
}

void handleRedirect() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  const String url = String("http://") + WiFi.softAPIP().toString() + "/";
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html", "");
  SendFn send(server);
  send("<!doctype html><html><head><meta charset='utf-8'>");
  send.SendFmt("<meta http-equiv='refresh' content='0; url=%s'>", url.c_str());
  send("</head><body>");
  send.SendFmt("<a href='%s'>Open portal</a>", url.c_str());
  send("</body></html>");
  send.Flush();
  server.sendContent("");
}

void handleResetExtrema() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  if (server.method() != HTTP_POST) {
    handleRedirect();
    return;
  }
  portENTER_CRITICAL(&g_state_mux);
  for (size_t i = 0; i < kPageCount; ++i) {
    g_state.page_recorded_min[i] = NAN;
    g_state.page_recorded_max[i] = NAN;
  }
  resetAllMax(g_state, millis());
  portEXIT_CRITICAL(&g_state_mux);
  handleRedirect();
}

void handleResetMaxOnly() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  if (server.method() != HTTP_POST) {
    handleRedirect();
    return;
  }
  portENTER_CRITICAL(&g_state_mux);
  resetAllMax(g_state, millis());
  portEXIT_CRITICAL(&g_state_mux);
  handleRedirect();
}

void handleResetMinOnly() {
  WebServer& server = WifiPortalServer();
  uint32_t& form_nonce = WifiPortalFormNonce();
  WifiDiagIncHttp();
  if (server.method() != HTTP_POST) {
    handleRedirect();
    return;
  }
  if (!server.hasArg("confirm_action") ||
      !server.hasArg("nonce") ||
      server.arg("nonce").toInt() != static_cast<long>(form_nonce)) {
    sendErrorHtml(server, 400, "Reset minima", "Confirmation required");
    return;
  }
  portENTER_CRITICAL(&g_state_mux);
  for (size_t i = 0; i < kPageCount; ++i) {
    g_state.page_recorded_min[i] = NAN;
  }
  portEXIT_CRITICAL(&g_state_mux);
  form_nonce = static_cast<uint32_t>(millis() ^ random(0xFFFFFFFF));
  handleRedirect();
}

void handleFactoryReset() {
  WebServer& server = WifiPortalServer();
  uint32_t& form_nonce = WifiPortalFormNonce();
  WifiDiagIncHttp();
  if (server.method() != HTTP_POST) {
    handleRedirect();
    return;
  }
  if (!server.hasArg("confirm_action") ||
      !server.hasArg("nonce") ||
      server.arg("nonce").toInt() != static_cast<long>(form_nonce)) {
    sendErrorHtml(server, 400, "Factory reset", "Confirmation required");
    return;
  }
  g_nvs.factoryResetClearAll();
  form_nonce = static_cast<uint32_t>(millis() ^ random(0xFFFFFFFF));
  server.send(200, "text/plain",
              "Factory reset, rebooting... (Wi-Fi password resets to default)");
  AppSleepMs(100);
  ESP.restart();
}

