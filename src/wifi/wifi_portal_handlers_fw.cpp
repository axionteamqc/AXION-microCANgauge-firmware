#include "wifi/wifi_portal_handlers.h"

#include <Update.h>
#include <WebServer.h>

#include "app/app_sleep.h"
#include "config/factory_config.h"
#include "config/logging.h"
#include "wifi/wifi_diag.h"
#include "wifi/wifi_portal_http.h"
#include "wifi/wifi_portal_internal.h"

namespace {

static bool s_fw_update_ok = false;
static bool s_fw_reject = false;
static const char* s_fw_msg = nullptr;
static size_t s_fw_update_written = 0;

}  // namespace

void handleFirmwarePage() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(200, "text/html", "");

  PortalWriter send(server);
  renderHtmlHead(send);
  send("<h2>Firmware Update</h2>");
  send.SendFmt("<p>FW: %s | Build: %s</p>",
               kFirmwareVersion, kBuildId);
  send("<p><strong>Do not remove power during update.</strong></p>");
  send.SendFmt("<form method='POST' action='/fw/update?nonce=%lu&confirm=1' "
               "enctype='multipart/form-data'>",
               static_cast<unsigned long>(WifiPortalFormNonce()));
  send("<p><label><input type='checkbox' id='confirm_all' name='confirm' value='1'> Confirm</label></p>");
  send("<input type='file' name='firmware' "
       "accept='.bin,application/octet-stream' required>");
  send("<div style='margin-top:10px;'>");
  send("<button type='submit' onclick=\"return submitAction('Upload firmware and reboot?');\">Upload</button>");
  send("</div></form>");
  renderHtmlFooter(send);
}

void handleFirmwareUpload() {
  WebServer& server = WifiPortalServer();
  HTTPUpload& upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START: {
      s_fw_update_ok = false;
      s_fw_reject = false;
      s_fw_msg = nullptr;
      s_fw_update_written = 0;
      if (!server.hasArg("nonce") ||
          server.arg("nonce").toInt() !=
              static_cast<long>(WifiPortalFormNonce())) {
        s_fw_reject = true;
        s_fw_msg = "Invalid nonce";
        break;
      }
      if (!server.hasArg("confirm") || server.arg("confirm") != "1") {
        s_fw_reject = true;
        s_fw_msg = "Confirmation required";
        break;
      }
      if (upload.name != "firmware") {
        s_fw_reject = true;
        s_fw_msg = "Invalid field";
        break;
      }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        s_fw_reject = true;
        s_fw_msg = "Update begin failed";
        LOGE("[FW] Update.begin failed err=%u\r\n",
             static_cast<unsigned>(Update.getError()));
      } else {
        wifi_ota_in_progress = true;
        LOGI("[FW] Update start: %s\r\n", upload.filename.c_str());
      }
      break;
    }
    case UPLOAD_FILE_WRITE: {
      if (s_fw_reject) break;
      const size_t written = Update.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        s_fw_reject = true;
        s_fw_msg = "Update write failed";
        LOGE("[FW] Update.write failed err=%u\r\n",
             static_cast<unsigned>(Update.getError()));
      } else {
        s_fw_update_written += written;
      }
      break;
    }
    case UPLOAD_FILE_END: {
      if (s_fw_reject) {
        Update.abort();
        wifi_ota_in_progress = false;
        break;
      }
      if (!Update.end(true)) {
        s_fw_reject = true;
        s_fw_msg = "Update end failed";
        LOGE("[FW] Update.end failed err=%u\r\n",
             static_cast<unsigned>(Update.getError()));
      } else {
        s_fw_update_ok = true;
        LOGI("[FW] Update end: %u bytes\r\n",
             static_cast<unsigned>(s_fw_update_written));
      }
      wifi_ota_in_progress = false;
      break;
    }
    case UPLOAD_FILE_ABORTED: {
      Update.end();
      s_fw_reject = true;
      s_fw_msg = "Update aborted";
      wifi_ota_in_progress = false;
      LOGE("[FW] Update aborted\r\n");
      break;
    }
    default:
      break;
  }
}

void handleFirmwareUpdate() {
  WebServer& server = WifiPortalServer();
  WifiDiagIncHttp();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  const bool ok = s_fw_update_ok && !s_fw_reject;
  server.send(ok ? 200 : 500, "text/html", "");

  PortalWriter send(server);
  renderHtmlHead(send);
  send("<h2>Firmware Update</h2>");
  if (ok) {
    send("<p>Update OK. Rebooting...</p>");
  } else {
    send("<p>Update failed. Please retry.</p>");
    if (s_fw_msg) {
      send("<p>");
      send(s_fw_msg);
      send("</p>");
    }
  }
  renderHtmlFooter(send);

  if (ok) {
    AppSleepMs(200);
    ESP.restart();
  }
}
