#include "wifi_pass_validate.h"

#include <cstring>

#ifndef ARDUINO
static size_t strlcpy_compat(char* dst, const char* src, size_t dst_len) {
  if (!dst || dst_len == 0) {
    return 0;
  }
  if (!src) {
    dst[0] = '\0';
    return 0;
  }
  size_t len = strlen(src);
  if (len >= dst_len) {
    len = dst_len - 1;
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
  return len;
}
#define strlcpy strlcpy_compat
#endif

bool ValidateWifiApPass(const char* s, char* err, size_t err_len) {
  auto writeErr = [&](const char* msg) {
    if (err && err_len > 0) {
      strlcpy(err, msg, err_len);
    }
  };
  if (!s) {
    writeErr("null");
    return false;
  }
  const size_t len = strlen(s);
  if (len < 8 || len > 16) {
    writeErr("length");
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    char c = s[i];
    if (c < 32 || c > 126) {
      writeErr("ascii");
      return false;
    }
    if (c == '"' || c == '<' || c == '>') {
      writeErr("chars");
      return false;
    }
  }
  writeErr("");
  return true;
}
