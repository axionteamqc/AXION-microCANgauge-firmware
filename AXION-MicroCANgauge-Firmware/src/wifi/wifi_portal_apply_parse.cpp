#include "wifi/wifi_portal_apply_internal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <cmath>
#include <cstdlib>

bool parseInt(const String& s, long& out) {
  char* end = nullptr;
  long v = strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0') return false;
  out = v;
  return true;
}

bool isPrintableAscii(const String& s) {
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c < 32 || c > 126) return false;
  }
  return true;
}

bool parseIntArg(WebServer& server, const char* name, long& out) {
  if (!server.hasArg(name)) return false;
  return parseInt(server.arg(name), out);
}

bool parseCheckboxArg(WebServer& server, const char* name) {
  return server.hasArg(name) && server.arg(name).length() > 0;
}

bool parseFloatArg(WebServer& server, const char* name, float& out) {
  if (!server.hasArg(name)) return false;
  String v = server.arg(name);
  v.trim();
  if (v.length() == 0) return false;
  char* end = nullptr;
  float f = strtof(v.c_str(), &end);
  if (end == v.c_str() || (end && *end != '\0') || isnan(f) || isinf(f)) {
    return false;
  }
  out = f;
  return true;
}

bool validatePageIndex(long v, size_t page_count) {
  return v >= 0 && static_cast<size_t>(v) < page_count;
}
