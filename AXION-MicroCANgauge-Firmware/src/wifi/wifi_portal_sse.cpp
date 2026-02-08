#include "wifi/wifi_portal_sse.h"

#include <WebServer.h>
#include <WiFi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "app/app_globals.h"
#include "app/app_ui_snapshot.h"
#include "config/factory_config.h"
#include "ui/pages.h"
#include "wifi/wifi_portal_escape.h"

namespace {

WiFiClient sse_client;
bool sse_active = false;
uint32_t sse_last_send_ms = 0;
uint32_t sse_frames_sent = 0;

struct SseWriter {
  static constexpr size_t kBufSize = 1024;
  explicit SseWriter(WiFiClient& client) : client_(client) {}
  ~SseWriter() { Flush(); }
  void SendRaw(const char* s) {
    if (!s) return;
    Append(s, strlen(s));
  }
  void SendFmt(const char* fmt, ...) {
    if (!fmt) return;
    char buf[128];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0) return;
    buf[sizeof(buf) - 1] = '\0';
    Append(buf, strlen(buf));
  }
  void SendChar(char c) { Append(&c, 1); }
  void Flush() {
    if (len_ == 0) return;
    client_.write(reinterpret_cast<const uint8_t*>(buf_), len_);
    len_ = 0;
  }

 private:
  void Append(const char* s, size_t len) {
    if (!s || len == 0) return;
    if (len >= kBufSize) {
      Flush();
      client_.write(reinterpret_cast<const uint8_t*>(s), len);
      return;
    }
    if (len_ + len > kBufSize) {
      Flush();
    }
    memcpy(buf_ + len_, s, len);
    len_ += len;
  }

  WiFiClient& client_;
  char buf_[kBufSize];
  size_t len_ = 0;
};

}  // namespace

void WifiPortalSseBegin(WebServer& server) {
  AppUiSnapshot ui;
  GetAppUiSnapshot(ui);
  if (!ui.wifi_mode_active) {
    server.send(503, "text/plain", "Wi-Fi mode inactive");
    return;
  }
  if (sse_active && sse_client.connected()) {
    server.send(503, "text/plain", "SSE busy");
    return;
  }
  WiFiClient client = server.client();
  if (!client) {
    server.send(503, "text/plain", "No client");
    return;
  }
  client.print(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-cache\r\n"
      "Connection: keep-alive\r\n\r\n");
  client.flush();
  sse_client = client;
  sse_client.setNoDelay(true);
  sse_active = true;
  sse_last_send_ms = 0;
  sse_client.print(":ok\n\n");
  sse_client.flush();
}

void WifiPortalSseTick(uint32_t now_ms) {
  if (!sse_active) return;
  if (!sse_client.connected()) {
    sse_client.stop();
    sse_active = false;
    return;
  }
  if (sse_last_send_ms != 0 &&
      (now_ms - sse_last_send_ms) < kWifiSseIntervalMs) {
    return;
  }

  AppUiSnapshot ui;
  GetAppUiSnapshot(ui);
  if (!ui.wifi_mode_active) return;
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  SseWriter out(sse_client);
  out.SendRaw("data: ");
  out.SendRaw("{\"can_ready\":");
  out.SendRaw(ui.can_ready ? "true" : "false");
  AppState page_state{};
  AppState::CanStats stats_snapshot{};
  portENTER_CRITICAL(&g_state_mux);
  stats_snapshot = g_state.can_stats;
  page_state.can_ready = g_state.can_ready;
  page_state.demo_mode = g_state.demo_mode;
  page_state.last_can_rx_ms = g_state.last_can_rx_ms;
  page_state.can_link = g_state.can_link;
  page_state.baro_acquired = g_state.baro_acquired;
  page_state.baro_kpa = g_state.baro_kpa;
  page_state.afr_show_lambda = g_state.afr_show_lambda;
  page_state.stoich_afr = g_state.stoich_afr;
  page_state.user_sensor[0] = g_state.user_sensor[0];
  page_state.user_sensor[1] = g_state.user_sensor[1];
  memcpy(page_state.last_good, g_state.last_good, sizeof(page_state.last_good));
  portEXIT_CRITICAL(&g_state_mux);
  out.SendRaw(",\"rx_total\":");
  out.SendFmt("%lu", static_cast<unsigned long>(stats_snapshot.rx_total));
  out.SendRaw(",\"rx_dash\":");
  out.SendFmt("%lu", static_cast<unsigned long>(stats_snapshot.rx_dash));
  const SignalRead map_r = ActiveStore().get(SignalId::kMap, now_ms);
  out.SendRaw(",\"map_age_ms\":");
  out.SendFmt("%lu", static_cast<unsigned long>(map_r.age_ms));
  out.SendRaw(",\"map_flags\":");
  out.SendFmt("%lu", static_cast<unsigned long>(map_r.flags));
  out.SendRaw(",\"map_ts_ms\":");
  out.SendFmt("%lu", static_cast<unsigned long>(
                          now_ms >= map_r.age_ms ? (now_ms - map_r.age_ms) : 0));
  out.SendRaw(",\"items\":[");
  for (size_t i = 0; i < page_count; ++i) {
    ScreenSettings cfg{};
    cfg.imperial_units =
        (ui.page_units_mask & (1U << static_cast<uint8_t>(i))) != 0;
    cfg.flip_180 = false;
    const PageRenderData d =
        BuildPageData(pages[i].id, page_state, cfg, ActiveStore(), now_ms);
    const PageMeta* meta = FindPageMeta(pages[i].id);
    const char* label = (meta && meta->label) ? meta->label : "PAGE";
    const char* unit = (d.unit) ? d.unit : "";
    if (i > 0) out.SendRaw(",");
    out.SendRaw("{\"index\":");
    out.SendFmt("%u", static_cast<unsigned>(i));
    out.SendRaw(",\"label\":\"");
    SendJsonEscapedGeneric([&](const char* p) { out.SendRaw(p); },
                           [&](char c) { out.SendChar(c); },
                           label);
    out.SendRaw("\",\"value\":\"");
    if (d.valid) {
      out.SendRaw(d.big);
      if (d.suffix[0] != '\0') out.SendRaw(d.suffix);
    } else {
    out.SendRaw("---");
    }
    out.SendRaw("\",\"unit\":\"");
    SendJsonEscapedGeneric([&](const char* p) { out.SendRaw(p); },
                           [&](char c) { out.SendChar(c); },
                           unit);
    out.SendRaw("\"}");
  }
  out.SendRaw("]}");
  out.SendRaw("\n\n");
  out.Flush();
  if ((sse_frames_sent & 0x3) == 0) {
    sse_client.flush();
  }
  sse_last_send_ms = now_ms;
  ++sse_frames_sent;
}

void WifiPortalSseStop() {
  if (sse_active) {
    if (sse_client) sse_client.stop();
    sse_active = false;
    sse_last_send_ms = 0;
  }
}
