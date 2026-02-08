#include "wifi/wifi_portal_handlers.h"

#include <WebServer.h>
#include <cmath>
#include <cstring>

#include "app/app_globals.h"
#include "app/app_ui_snapshot.h"
#include "app_state.h"
#include "config/factory_config.h"
#include "config/logging.h"
#include "ui/pages.h"
#include "wifi/wifi_diag.h"
#include "wifi/wifi_portal_escape.h"
#include "wifi/wifi_portal_http.h"
#include "wifi/wifi_portal_internal.h"
#include "wifi/wifi_portal_sse.h"
#include "wifi/wifi_portal_units.h"

namespace {

using SendFn = PortalWriter;

const char* FormatCsvFloat(char* out, size_t n, float v, unsigned decimals) {
  if (n == 0) return out;
  if (!isfinite(v)) {
    out[0] = '\0';
    return out;
  }
  dtostrf(static_cast<double>(v), 0, decimals, out);
  char* p = out;
  while (*p == ' ') ++p;
  if (p != out) {
    const size_t len = strlen(p);
    memmove(out, p, len + 1);
  }
  return out;
}

const char* FormatCsvValue(char* out, size_t n, ValueKind kind, float canon,
                           const ScreenSettings& cfg) {
  if (!isfinite(canon)) {
    if (n > 0) out[0] = '\0';
    return out;
  }
  const float disp = CanonToDisplay(kind, canon, cfg);
  const bool one_dec = (kind == ValueKind::kPressure || kind == ValueKind::kBoost ||
                        kind == ValueKind::kVoltage || kind == ValueKind::kAfr ||
                        kind == ValueKind::kPercent);
  return FormatCsvFloat(out, n, disp, one_dec ? 1u : 0u);
}

const char* CsvEscapeField(char* dst, size_t dst_n, const char* s) {
  if (dst_n == 0) return dst;
  if (!s) {
    dst[0] = '\0';
    return dst;
  }
  bool needs_quotes = false;
  for (const char* p = s; *p; ++p) {
    if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) {
    strlcpy(dst, s, dst_n);
    return dst;
  }
  size_t out_len = 0;
  if (out_len + 1 < dst_n) {
    dst[out_len++] = '"';
  }
  for (const char* p = s; *p && out_len + 1 < dst_n; ++p) {
    if (*p == '"') {
      if (out_len + 2 >= dst_n) break;
      dst[out_len++] = '"';
      dst[out_len++] = '"';
    } else {
      dst[out_len++] = *p;
    }
  }
  if (out_len + 1 < dst_n) {
    dst[out_len++] = '"';
  }
  dst[out_len < dst_n ? out_len : (dst_n - 1)] = '\0';
  return dst;
}

}  // namespace

void handleReportCsv() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  AppUiSnapshot ui;
  GetAppUiSnapshot(ui);
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"report.csv\"");
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Connection", "close");
  server.send(200, "text/csv", "");
  SendFn send(server);
  send("page,label,units,rec_min,rec_max,thr_min,thr_max,alert_max,alert_min\r\n");
  int rows_written = 0;
  const size_t pages_n = (page_count < static_cast<size_t>(kPageCount))
                             ? page_count
                             : static_cast<size_t>(kPageCount);
  for (size_t i = 0; i < pages_n; ++i) {
    const PageMeta* meta = FindPageMeta(pages[i].id);
    const ValueKind kind = meta ? meta->kind : ValueKind::kNone;
    ScreenSettings cfg{};
    cfg.imperial_units =
        (ui.page_units_mask & (1U << static_cast<uint8_t>(i))) != 0;
    cfg.flip_180 = false;
    const char* units = unitsForKind(kind, cfg.imperial_units);
    const float rec_min = ui.page_recorded_min[i];
    const float rec_max = ui.page_recorded_max[i];
    const float thr_min = ui.thresholds[i].min;
    const float thr_max = ui.thresholds[i].max;
    const bool alert_max =
        (ui.page_alert_max_mask & (1U << static_cast<uint8_t>(i))) != 0;
    const bool alert_min =
        (ui.page_alert_min_mask & (1U << static_cast<uint8_t>(i))) != 0;
    const char* label = (meta && meta->label) ? meta->label : "PAGE";
    char label_csv[96];
    char units_csv[32];
    CsvEscapeField(label_csv, sizeof(label_csv), label);
    CsvEscapeField(units_csv, sizeof(units_csv), units);
    char rec_min_s[32];
    char rec_max_s[32];
    char thr_min_s[32];
    char thr_max_s[32];
    FormatCsvValue(rec_min_s, sizeof(rec_min_s), kind, rec_min, cfg);
    FormatCsvValue(rec_max_s, sizeof(rec_max_s), kind, rec_max, cfg);
    FormatCsvValue(thr_min_s, sizeof(thr_min_s), kind, thr_min, cfg);
    FormatCsvValue(thr_max_s, sizeof(thr_max_s), kind, thr_max, cfg);
    char line[320];
    snprintf(line, sizeof(line), "%u,%s,%s,%s,%s,%s,%s,%s,%s\r\n",
             static_cast<unsigned int>(i), label_csv, units_csv, rec_min_s,
             rec_max_s,
             thr_min_s, thr_max_s, alert_max ? "ON" : "OFF",
             alert_min ? "ON" : "OFF");
    send(line);
    ++rows_written;
  }
  if (kEnableVerboseSerialLogs) {
    LOGI("[REPORT] rows=%d\r\n", rows_written);
  }
  send.Flush();
  server.sendContent("");
}

void handleConfigJson() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  AppUiSnapshot ui;
  GetAppUiSnapshot(ui);
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"config.json\"");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "");
  SendFn send(server);
  char num_buf[32];
  auto appendUInt = [&](uint32_t v) {
    snprintf(num_buf, sizeof(num_buf), "%lu", static_cast<unsigned long>(v));
    send.SendRaw(num_buf);
  };
  auto appendFloat = [&](double v, int decimals) {
    const char* fmt = (decimals == 1) ? "%.1f" : (decimals == 3 ? "%.3f" : "%.0f");
    snprintf(num_buf, sizeof(num_buf), fmt, v);
    send.SendRaw(num_buf);
  };
  send.SendRaw("{\"schema\":\"wifi_config_v1\",");
  send.SendRaw("\"display_topology\":");
  appendUInt(static_cast<uint8_t>(ui.display_topology));
  send.SendRaw(",");
  send.SendRaw("\"flip0\":");
  send.SendRaw(ui.screen_flip[0] ? "true" : "false");
  send.SendRaw(",");
  send.SendRaw("\"flip1\":");
  send.SendRaw(ui.screen_flip[1] ? "true" : "false");
  send.SendRaw(",");
  send.SendRaw("\"can_bitrate_value\":");
  appendUInt(ui.can_bitrate_value);
  send.SendRaw(",");
  send.SendRaw("\"can_bitrate_locked\":");
  send.SendRaw(ui.can_bitrate_locked ? "true" : "false");
  send.SendRaw(",");
  send.SendRaw("\"boot_pages\":[");
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    if (z > 0) send.SendRaw(",");
    appendUInt(ui.boot_page_index[z]);
  }
  send.SendRaw("],");
  send.SendRaw("\"pages\":[");
  for (size_t i = 0; i < page_count; ++i) {
    const PageMeta* meta = FindPageMeta(pages[i].id);
    ScreenSettings cfg{};
    cfg.imperial_units =
        (ui.page_units_mask & (1U << static_cast<uint8_t>(i))) != 0;
    cfg.flip_180 = false;
    const ValueKind kind = meta ? meta->kind : ValueKind::kNone;
    auto appendVal = [&](float v) {
      if (isnan(v)) {
        send.SendRaw("null");
        return;
      }
      const float disp = CanonToDisplay(kind, v, cfg);
      const bool one_dec =
          (kind == ValueKind::kPressure || kind == ValueKind::kBoost ||
           kind == ValueKind::kVoltage || kind == ValueKind::kAfr ||
           kind == ValueKind::kPercent);
      appendFloat(static_cast<double>(disp), one_dec ? 1 : 0);
    };
    const bool alert_max =
        (ui.page_alert_max_mask & (1U << static_cast<uint8_t>(i))) != 0;
    const bool alert_min =
        (ui.page_alert_min_mask & (1U << static_cast<uint8_t>(i))) != 0;
    if (i > 0) send.SendRaw(",");
    send.SendRaw("{");
    send.SendRaw("\"index\":");
    appendUInt(static_cast<uint32_t>(i));
    send.SendRaw(",");
    send.SendRaw("\"label\":\"");
    SendJsonEscaped(send, (meta && meta->label) ? meta->label : "PAGE");
    send.SendRaw("\",");
    send.SendRaw("\"units\":\"");
    SendJsonEscaped(send, unitsForKind(kind, cfg.imperial_units));
    send.SendRaw("\",");
    send.SendRaw("\"rec_min\":");
    appendVal(ui.page_recorded_min[i]);
    send.SendRaw(",");
    send.SendRaw("\"rec_max\":");
    appendVal(ui.page_recorded_max[i]);
    send.SendRaw(",");
    send.SendRaw("\"thr_min\":");
    appendVal(ui.thresholds[i].min);
    send.SendRaw(",");
    send.SendRaw("\"thr_max\":");
    appendVal(ui.thresholds[i].max);
    send.SendRaw(",");
    send.SendRaw("\"alert_max\":");
    send.SendRaw(alert_max ? "true" : "false");
    send.SendRaw(",");
    send.SendRaw("\"alert_min\":");
    send.SendRaw(alert_min ? "true" : "false");
    send.SendRaw("}");
  }
  send.SendRaw("],");
  send.SendRaw("\"user_sensors\":[");
  for (uint8_t i = 0; i < 2; ++i) {
    const UserSensorCfg& us = ui.user_sensor[i];
    if (i > 0) send.SendRaw(",");
    send.SendRaw("{");
    send.SendRaw("\"preset\":");
    appendUInt(static_cast<uint8_t>(us.preset));
    send.SendRaw(",");
    send.SendRaw("\"kind\":");
    appendUInt(static_cast<uint8_t>(us.kind));
    send.SendRaw(",");
    send.SendRaw("\"source\":");
    appendUInt(static_cast<uint8_t>(us.source));
    send.SendRaw(",");
    send.SendRaw("\"label\":\"");
    SendJsonEscaped(send, us.label);
    send.SendRaw("\",");
    send.SendRaw("\"scale\":");
    appendFloat(static_cast<double>(us.scale), 3);
    send.SendRaw(",");
    send.SendRaw("\"offset\":");
    appendFloat(static_cast<double>(us.offset), 3);
    send.SendRaw(",");
    send.SendRaw("\"unit_metric\":\"");
    SendJsonEscaped(send, us.unit_metric);
    send.SendRaw("\",");
    send.SendRaw("\"unit_imperial\":\"");
    SendJsonEscaped(send, us.unit_imperial);
    send.SendRaw("\"");
    send.SendRaw("}");
  }
  send.SendRaw("],");
  send.SendRaw("\"stoich_afr\":");
  appendFloat(static_cast<double>(ui.stoich_afr), 1);
  send.SendRaw(",");
  send.SendRaw("\"afr_show_lambda\":");
  send.SendRaw(ui.afr_show_lambda ? "true" : "false");
  send.SendRaw("}");
  send.Flush();
  server.sendContent("");
}

void handleLivePage() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", "");
  SendFn send(server);
  send("<!doctype html><html><head><meta charset='utf-8'>");
  send("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  send("<style>"
       "body{margin:0;padding:12px;font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;"
       "background:#f7f7f8;color:#111;font-size:16px;}"
       ".card{max-width:520px;margin:0 auto;background:#fff;border:1px solid #d8dbe2;border-radius:14px;"
       "padding:14px;box-shadow:0 6px 18px rgba(0,0,0,0.06);}"
       "h1{font-size:1.3rem;margin:6px 0 10px;}button,a.btn{display:inline-block;padding:10px 14px;"
       "border-radius:10px;border:1px solid #d8dbe2;background:#f3f4f6;color:#111;text-decoration:none;"
       "font-size:16px;margin:6px 6px 12px 0;}"
       "table{border-collapse:collapse;width:100%;}"
       "th,td{border:1px solid #d8dbe2;padding:8px 10px;text-align:left;vertical-align:middle;}"
       "th{background:#f0f1f4;}"
       ".status{font-size:0.95rem;color:#555;margin-bottom:8px;}"
       "</style></head><body><div class='card'>");
  send("<h1>Live Data</h1>");
  send("<div class='status'>Status: <span id='live_status'>Disconnected</span></div>");
  send("<table><thead><tr><th>#</th><th>Label</th><th>Value</th><th>Unit</th></tr></thead><tbody>");
  for (size_t i = 0; i < page_count; ++i) {
    const PageMeta* meta = FindPageMeta(pages[i].id);
    const char* label = (meta && meta->label) ? meta->label : "PAGE";
    char idx_buf[16];
    snprintf(idx_buf, sizeof(idx_buf), "%u", static_cast<unsigned>(i));
    send("<tr data-idx='");
    send(idx_buf);
    send("'><td>");
    send(idx_buf);
    send("</td><td>");
    send(label);
    send("</td><td id='val");
    send(idx_buf);
    send("'>---</td><td id='unit");
    send(idx_buf);
    send("'></td></tr>");
  }
  send("</tbody></table>");
  send("<a class='btn' href='/'>Back</a>");
  send("</div><script>"
       "const statusEl=document.getElementById('live_status');"
       "function setStatus(s){statusEl.textContent=s;}"
       "const es=new EventSource('/live/events');"
       "es.onmessage=function(ev){"
       " try{const data=JSON.parse(ev.data);"
       "  const arr=Array.isArray(data)?data:(data.items||[]);"
       "  arr.forEach(function(it){"
       "    const v=document.getElementById('val'+it.index);"
       "    const u=document.getElementById('unit'+it.index);"
       "    if(v){v.textContent=it.value;}"
       "    if(u){u.textContent=it.unit;}"
       "  });"
       "  if(data && !Array.isArray(data)){"
       "    const rt=data.rx_total!==undefined?data.rx_total:'';"
       "    const rd=data.rx_dash!==undefined?data.rx_dash:'';"
       "    const cr=data.can_ready?'CAN READY':'CAN OFF';"
       "    setStatus('Connected '+cr+' RX:'+rt+'/'+rd);"
       "  }else{setStatus('Connected');}"
       " }catch(e){setStatus('Parse error');}"
       "};"
       "es.onerror=function(){setStatus('Disconnected');};"
       "</script></body></html>");
  send.Flush();
  server.sendContent("");
}

void handleLiveEvents() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  WifiPortalSseBegin(server);
}
