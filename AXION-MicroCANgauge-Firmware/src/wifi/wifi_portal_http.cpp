#include "wifi/wifi_portal_http.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "config/logging.h"
#include "wifi/wifi_ap_pass.h"
#include "wifi/wifi_portal_handlers.h"

PortalWriter::PortalWriter(WebServer& server, size_t* bytes_sent)
    : server_(server), bytes_sent_(bytes_sent) {
  buf_[0] = '\0';
  buf_len_ = 0;
}

PortalWriter::~PortalWriter() { Flush(); }

void PortalWriter::Append(const char* s, size_t len) const {
  if (!s || len == 0) return;
  if (len >= kBufSize) {
    Flush();
    server_.sendContent(s);
    return;
  }
  if (buf_len_ + len > kBufSize) {
    Flush();
  }
  memcpy(buf_ + buf_len_, s, len);
  buf_len_ += len;
}

void PortalWriter::Flush() const {
  if (buf_len_ == 0) return;
  buf_[buf_len_] = '\0';
  server_.sendContent(buf_);
  buf_len_ = 0;
}

void PortalWriter::SendRaw(const char* s) const {
  if (!s) return;
  const size_t len = strlen(s);
  if (bytes_sent_) {
    *bytes_sent_ += len;
  }
  Append(s, len);
}

void PortalWriter::SendString(const String& s) const {
  if (bytes_sent_) {
    *bytes_sent_ += s.length();
  }
  Append(s.c_str(), s.length());
}

void PortalWriter::SendFmt(const char* fmt, ...) const {
  if (!fmt) return;
  char buf[256];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n <= 0) return;
  buf[sizeof(buf) - 1] = '\0';
  const size_t len = strlen(buf);
  if (bytes_sent_) {
    *bytes_sent_ += len;
  }
  Append(buf, len);
}

// Shared logging helper
static void LogHttp(WebServer& srv) {
  auto methodStr = [&]() -> const char* {
    switch (srv.method()) {
      case HTTP_GET: return "GET";
      case HTTP_POST: return "POST";
      case HTTP_PUT: return "PUT";
      case HTTP_DELETE: return "DELETE";
      case HTTP_OPTIONS: return "OPTIONS";
      default: return "OTHER";
    }
  };
  int len = -1;
  if (srv.hasHeader("Content-Length")) {
    len = srv.header("Content-Length").toInt();
  }
  LOGI("[HTTP] %s %s len=%d from=%s t=%lu\n",
       methodStr(), srv.uri().c_str(), len,
       srv.client().remoteIP().toString().c_str(),
       static_cast<unsigned long>(millis()));
  (void)methodStr;
  (void)len;
}

// Route registration
void WifiPortalHttpRegisterRoutes(WebServer& server) {
  server.on("/", [&server]() {
    LogHttp(server);
    handleRoot();
  });
  auto logRedirect = [&server]() {
    LogHttp(server);
    handleRedirect();
  };
  server.on("/generate_204", logRedirect);
  server.on("/gen_204", logRedirect);
  server.on("/hotspot-detect.html", logRedirect);
  server.on("/library/test/success.html", logRedirect);
  server.on("/ncsi.txt", logRedirect);
  server.on("/connecttest.txt", logRedirect);
  server.on("/success.txt", logRedirect);
  server.on("/redirect", logRedirect);
  server.on("/favicon.ico", logRedirect);
  server.on("/wpad.dat", [&server]() {
    LogHttp(server);
    server.send(204, "text/plain", "");
  });
  server.on("/download/report.csv", [&server]() {
    LogHttp(server);
    handleReportCsv();
  });
  server.on("/download/config.json", [&server]() {
    LogHttp(server);
    handleConfigJson();
  });
  server.on("/fw", HTTP_GET, [&server]() {
    LogHttp(server);
    handleFirmwarePage();
  });
  server.on("/fw/update", HTTP_POST, [&server]() {
    LogHttp(server);
    handleFirmwareUpdate();
  }, []() {
    handleFirmwareUpload();
  });
  server.on("/live", [&server]() {
    LogHttp(server);
    handleLivePage();
  });
  server.on("/live/events", [&server]() {
    LogHttp(server);
    handleLiveEvents();
  });
  server.on("/apply", HTTP_POST, [&server]() {
    LogHttp(server);
    handleApply();
  });
  server.onNotFound([&server]() {
    LogHttp(server);
    handleRedirect();
  });
}


// HTML header + base styling + JS helpers
void renderHtmlHead(const PortalWriter& send) {
  send("<!doctype html><html><head><meta charset='utf-8'>");
  send("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  send("<style>"
       ":root{--bg0:#f7f7f8;--bg1:#e9eaee;--card:#ffffff;--text:#111;--muted:#555;"
       "--border:#d8dbe2;}"
       "html,body{height:100%;}"
       "body{margin:0;color:var(--text);font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;"
       "background:#f5f5f5;padding:12px;font-size:16px;}"
       "main{max-width:520px;margin:0 auto;background:var(--card);border:1px solid var(--border);"
       "border-radius:14px;padding:14px;box-shadow:0 6px 18px rgba(0,0,0,0.06);}"
       "h1,h2,h3{margin:10px 0 8px;}h1{font-size:1.35rem;}h2{font-size:1.10rem;}h3{font-size:1.00rem;}"
       "p,small{color:var(--muted);font-size:1rem;}"
       "label{display:block;margin:10px 0 6px;font-weight:600;font-size:1rem;}"
       "input[type=\"text\"],input[type=\"number\"],select{width:100%;box-sizing:border-box;font-size:16px;"
       "padding:10px 10px;border:1px solid var(--border);border-radius:10px;background:#fff;}"
       "input[type=\"checkbox\"]{transform:scale(1.2);margin-right:10px;}"
       ".boot-row{display:flex;align-items:center;gap:12px;flex-wrap:wrap;}"
       ".boot-row label{flex:0 0 160px;min-width:120px;}"
       ".boot-row select{flex:1;min-width:120px;}"
       ".checklist{display:grid;gap:10px;margin:12px 0;}"
       ".check-row{display:flex;align-items:center;gap:10px;font-weight:600;}"
       "button{font-size:16px;padding:10px 12px;border-radius:10px;border:1px solid var(--border);"
       "background:#f3f4f6;cursor:pointer;margin:6px 8px 6px 0;}"
       "button.primary{background:#111;color:#fff;border-color:#111;}"
       "a.bigbtn{display:block;width:100%;text-align:center;font-size:20px;padding:16px 14px;"
       "border-radius:14px;background:#111;color:#fff;text-decoration:none;margin-top:14px;}"
       "details{margin-top:12px;border:1px solid var(--border);border-radius:12px;padding:10px;background:#fff;}"
       "summary{font-weight:700;cursor:pointer;}"
       "table{border-collapse:collapse;width:100%;}"
       ".table-wrap{overflow-x:auto;-webkit-overflow-scrolling:touch;margin:10px 0 14px;}"
       "table.wide{width:100%;min-width:920px;}"
       "th,td{border:1px solid var(--border);padding:10px 12px;text-align:left;vertical-align:middle;}"
       "th{background:#f0f1f4;white-space:nowrap;}"
       "td select{min-width:92px;}"
       "</style></head><body><main>");
  send("<script>"
       "function submitAction(msg){"
       "var cb=document.getElementById('confirm_all');"
       "if(!cb||!cb.checked){alert('Check confirmation first');return false;}"
       "if(!msg) return true;"
       "return confirm(msg);}"
       "function syncBootPages(){"
       "var s0=document.getElementById('boot_page_z0_sel');"
       "var s1=document.getElementById('boot_page_z1_sel');"
       "var s2=document.getElementById('boot_page_z2_sel');"
       "var h0=document.getElementById('boot_page_z0');"
       "var h1=document.getElementById('boot_page_z1');"
       "var h2=document.getElementById('boot_page_z2');"
       "if(s0&&h0) h0.value=s0.value;"
       "if(s1&&h1) h1.value=s1.value;"
       "if(s2&&h2) h2.value=s2.value;"
       "}"
       "function setAllAlerts(on){"
       "const boxes=document.querySelectorAll("
       "'input[type=\"checkbox\"][name^=\"amax_\"],input[type=\"checkbox\"][name^=\"amin_\"]');"
       "boxes.forEach(function(b){b.checked=!!on;});"
       "}"
       "function toggleDemo(chk){"
       "document.getElementById('demo_mode').checked=chk.checked;"
       "}"
       "</script>");
  send("<h1>AXION Wi-Fi mode</h1>");
  send.SendFmt("<p class='small'>Device AP: %s (pass: %s)</p>",
               WifiApSsid(), WifiApPass());
  send("<p class='small'>Open: http://axion.local/ or http://192.168.4.1/</p>");
}

void renderHtmlFooter(const PortalWriter& send) {
  send("</main></body></html>");
}
