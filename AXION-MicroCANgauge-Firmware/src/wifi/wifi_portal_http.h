#pragma once

#include <Arduino.h>
#include <WebServer.h>

struct PortalWriter {
  explicit PortalWriter(WebServer& server, size_t* bytes_sent = nullptr);
  ~PortalWriter();
  void SendRaw(const char* s) const;
  void SendFmt(const char* fmt, ...) const;
  void SendString(const String& s) const;
  void Flush() const;
  void operator()(const char* s) const { SendRaw(s); }
  void operator()(const String& s) const { SendString(s); }

 private:
  WebServer& server_;
  size_t* bytes_sent_;
  static constexpr size_t kBufSize = 1024;
  mutable char buf_[kBufSize + 1];
  mutable size_t buf_len_ = 0;
  void Append(const char* s, size_t len) const;
};

void WifiPortalHttpRegisterRoutes(WebServer& server);

void renderHtmlHead(const PortalWriter& send);
void renderHtmlFooter(const PortalWriter& send);

