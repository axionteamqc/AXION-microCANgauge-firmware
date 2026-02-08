#pragma once

#include "wifi/wifi_portal_http.h"

// HTML escaping for user-provided strings.
inline void SendHtmlEscaped(const PortalWriter& send, const char* s) {
  if (!s) return;
  char buf[32];
  size_t pos = 0;
  auto flush = [&]() {
    if (pos == 0) return;
    buf[pos] = '\0';
    send.SendRaw(buf);
    pos = 0;
  };
  for (const char* p = s; *p; ++p) {
    const char c = *p;
    const char* esc = nullptr;
    switch (c) {
      case '&':
        esc = "&amp;";
        break;
      case '<':
        esc = "&lt;";
        break;
      case '>':
        esc = "&gt;";
        break;
      case '"':
        esc = "&quot;";
        break;
      case '\'':
        esc = "&#39;";
        break;
      default:
        break;
    }
    if (esc) {
      flush();
      send.SendRaw(esc);
    } else {
      if (pos + 1 >= sizeof(buf)) {
        flush();
      }
      buf[pos++] = c;
    }
  }
  flush();
}

// Generic JSON escaping helper using raw/char writers.
template <typename WriteRawFn, typename WriteCharFn>
inline void SendJsonEscapedGeneric(WriteRawFn write_raw,
                                   WriteCharFn write_char,
                                   const char* s) {
  if (!s) return;
  static const char kHex[] = "0123456789ABCDEF";
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
       *p != '\0'; ++p) {
    const unsigned char c = *p;
    switch (c) {
      case '\\':
        write_raw("\\\\");
        break;
      case '\"':
        write_raw("\\\"");
        break;
      case '\n':
        write_raw("\\n");
        break;
      case '\r':
        write_raw("\\r");
        break;
      case '\t':
        write_raw("\\t");
        break;
      default:
        if (c < 0x20) {
          char buf[7];
          buf[0] = '\\';
          buf[1] = 'u';
          buf[2] = '0';
          buf[3] = '0';
          buf[4] = kHex[(c >> 4) & 0x0F];
          buf[5] = kHex[c & 0x0F];
          buf[6] = '\0';
          write_raw(buf);
        } else {
          write_char(static_cast<char>(c));
        }
        break;
    }
  }
}

// JSON escaping for Print sinks (SSE).
inline void PrintJsonEscaped(Print& out, const char* s) {
  SendJsonEscapedGeneric([&](const char* p) { out.print(p); },
                         [&](char c) { out.write(c); }, s);
}

// JSON escaping for PortalWriter streaming.
inline void SendJsonEscaped(const PortalWriter& send, const char* s) {
  SendJsonEscapedGeneric([&](const char* p) { send.SendRaw(p); },
                         [&](char c) {
                           char tmp[2] = {c, '\0'};
                           send.SendRaw(tmp);
                         },
                         s);
}
