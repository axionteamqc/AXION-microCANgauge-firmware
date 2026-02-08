#include "wifi/wifi_portal_apply_internal.h"

#include <Arduino.h>
#include <WebServer.h>

#include "app/app_globals.h"
#include "app/app_sleep.h"
#include "ui/pages.h"
#include "wifi/wifi_portal_http.h"

namespace {
void SendUint(const PortalWriter& send, uint32_t value) {
  send.SendFmt("%lu", static_cast<unsigned long>(value));
}
}  // namespace

void SendApplySuccessResponse(WebServer& server,
                              const uint8_t boot_pages_internal[kMaxZones]) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  PortalWriter send(server);
  send.SendRaw("<html><body><h1>OK</h1><p>Applying and rebooting...</p>"
               "<h3>Applied boot pages</h3><ul>");
  size_t resp_page_count = 0;
  const PageDef* resp_pages = GetPageTable(resp_page_count);
  auto labelFor = [&](uint8_t idx) -> const char* {
    if (idx >= resp_page_count) return "PAGE";
    const PageMeta* meta = FindPageMeta(resp_pages[idx].id);
    return (meta && meta->label) ? meta->label : "PAGE";
  };
  send.SendRaw("<li>Received: Z0=");
  SendUint(send, boot_pages_internal[0]);
  send.SendRaw(" (");
  send.SendRaw(labelFor(boot_pages_internal[0]));
  send.SendRaw(") Z1=");
  SendUint(send, boot_pages_internal[1]);
  send.SendRaw(" (");
  send.SendRaw(labelFor(boot_pages_internal[1]));
  send.SendRaw(") Z2=");
  SendUint(send, boot_pages_internal[2]);
  send.SendRaw(" (");
  send.SendRaw(labelFor(boot_pages_internal[2]));
  send.SendRaw(")</li>");
  send.SendRaw("<li>Saved: Z0=");
  SendUint(send, g_state.boot_page_index[0]);
  send.SendRaw(" (");
  send.SendRaw(labelFor(g_state.boot_page_index[0]));
  send.SendRaw(") Z1=");
  SendUint(send, g_state.boot_page_index[1]);
  send.SendRaw(" (");
  send.SendRaw(labelFor(g_state.boot_page_index[1]));
  send.SendRaw(") Z2=");
  SendUint(send, g_state.boot_page_index[2]);
  send.SendRaw(" (");
  send.SendRaw(labelFor(g_state.boot_page_index[2]));
  send.SendRaw(")</li>");
  send.SendRaw("</ul></body></html>");
}
