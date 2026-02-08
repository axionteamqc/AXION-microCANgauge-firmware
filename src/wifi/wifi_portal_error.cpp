#include "wifi/wifi_portal_error.h"

#include "wifi/wifi_portal_escape.h"
#include "wifi/wifi_portal_http.h"

void sendErrorHtml(WebServer& server, int /*code*/, const String& title,
                   const String& msg) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  PortalWriter send(server);
  send.SendRaw("<html><head><title>");
  SendHtmlEscaped(send, title.c_str());
  send.SendRaw("</title></head><body><h2>");
  SendHtmlEscaped(send, title.c_str());
  send.SendRaw("</h2><p>");
  SendHtmlEscaped(send, msg.c_str());
  send.SendRaw("</p><p><button onclick=\"window.history.back()\">Back to config</button></p></body></html>");
  send.Flush();
  server.sendContent("");
}
