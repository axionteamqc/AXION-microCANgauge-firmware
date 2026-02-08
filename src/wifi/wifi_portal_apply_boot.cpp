#include "wifi/wifi_portal_apply_internal.h"

#include <WebServer.h>

#include "config/factory_config.h"
#include "config/logging.h"
#include "wifi/wifi_portal_error.h"

const char* const kFieldBootPage[kMaxZones] = {
    "boot_page_z0",
    "boot_page_z1",
    "boot_page_z2",
};

namespace {
bool ParsePageValue(const String& val, size_t page_count, uint8_t& out) {
  long v = 0;
  if (!parseInt(val, v) || !validatePageIndex(v, page_count)) return false;
  out = static_cast<uint8_t>(v);
  return true;
}
}  // namespace

bool ParseBootPages(WebServer& server, size_t page_count,
                    uint8_t (&boot_pages_internal)[kMaxZones],
                    bool& warned_boot_page) {
  String key;
  key.reserve(32);
  bool missing_boot = false;
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    const char* field = kFieldBootPage[z];
    // Prefer hidden field, but accept legacy direct name as well.
    key = field;
    const bool has_hidden = server.hasArg(key);
    String hidden_val;
    if (has_hidden) {
      hidden_val = server.arg(key);
    }
    key = field;
    key += "_sel";
    const bool has_sel = server.hasArg(key);
    String sel_val;
    if (has_sel) {
      sel_val = server.arg(key);
    }
    const bool has_field = has_hidden || has_sel;
    if (kEnableVerboseSerialLogs) {
      LOGV("boot_page_z%u: hidden_present=%s sel_present=%s hidden_val=%s sel_val=%s\r\n",
           z, has_hidden ? "yes" : "no",
           has_sel ? "yes" : "no",
           has_hidden ? hidden_val.c_str() : "-",
           has_sel ? sel_val.c_str() : "-");
    }
    if (!has_field) {
      missing_boot = true;
      continue;
    }
    if (has_hidden) {
      if (!ParsePageValue(hidden_val, page_count, boot_pages_internal[z])) {
        if (!warned_boot_page) {
          warned_boot_page = true;
          LOGW("[APPLY] invalid: boot page index zone=%u\r\n",
               static_cast<unsigned>(z));
        }
        sendErrorHtml(server, 400, "Apply error", "Invalid boot page index");
        return false;
      }
    } else {
      if (!ParsePageValue(sel_val, page_count, boot_pages_internal[z])) {
        if (!warned_boot_page) {
          warned_boot_page = true;
          LOGW("[APPLY] invalid: boot page index zone=%u\r\n",
               static_cast<unsigned>(z));
        }
        sendErrorHtml(server, 400, "Apply error", "Invalid boot page index");
        return false;
      }
    }
  }
  if (missing_boot) {
    if (!warned_boot_page) {
      warned_boot_page = true;
      LOGW("[APPLY] invalid: missing boot page field(s)\r\n");
    }
    static String msg;
    msg.reserve(96);
    msg = "Missing fields: ";
    bool first = true;
    for (uint8_t z = 0; z < kMaxZones; ++z) {
      char field[20];
      char field_sel[24];
      snprintf(field, sizeof(field), "boot_page_z%u",
               static_cast<unsigned>(z));
      snprintf(field_sel, sizeof(field_sel), "boot_page_z%u_sel",
               static_cast<unsigned>(z));
      const bool has_hidden = server.hasArg(field);
      const bool has_sel = server.hasArg(field_sel);
      if (!has_hidden && !has_sel) {
        if (!first) msg += ", ";
        msg += field;
        first = false;
      }
    }
    sendErrorHtml(server, 400, "Apply error", msg);
    return false;
  }
  return true;
}
