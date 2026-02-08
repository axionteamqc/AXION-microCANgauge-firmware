#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>

// Minimal DNSServer clone with request counting.

enum class DiagDnsReplyCode {
  kNoError = 0,
  kFormError = 1,
  kServerFailure = 2,
  kNonExistentDomain = 3,
  kNotImplemented = 4,
  kRefused = 5,
  kYXDomain = 6,
  kYXRRSet = 7,
  kNXRRSet = 8
};

class DiagDnsServer {
 public:
  DiagDnsServer();
  ~DiagDnsServer();

  bool start(uint16_t port, const String& domain_name, const IPAddress& resolved);
  void stop();
  void processNextRequest();

  void setErrorReplyCode(DiagDnsReplyCode code);
  void setTTL(uint32_t ttl_seconds);

  uint32_t request_count() const { return request_count_; }

 private:
  static constexpr size_t kMaxDnsPacket = 512;
  void downcaseAndRemoveWwwPrefix(String& domain_name);
  void replyWithIP();
  void replyWithCustomCode();
  void stopInternal();

  WiFiUDP udp_;
  uint16_t port_ = 0;
  String domain_name_;
  String req_name_;
  uint8_t resolved_ip_[4] = {0};
  int current_packet_size_ = 0;
  uint8_t buffer_[kMaxDnsPacket] = {};
  uint32_t ttl_ = 60;
  DiagDnsReplyCode error_reply_code_ = DiagDnsReplyCode::kNonExistentDomain;
  uint32_t request_count_ = 0;
};
