#include "wifi/diag_dns_server.h"

#include <lwip/def.h>
#include "config/logging.h"
#include "wifi/wifi_diag.h"

#define DNS_QR_RESPONSE 1
#define DNS_OPCODE_QUERY 0
#define DNS_DEFAULT_TTL 60

#pragma pack(push, 1)
struct DnsHeader {
  uint16_t ID;
  uint8_t RD : 1;
  uint8_t TC : 1;
  uint8_t AA : 1;
  uint8_t OPCode : 4;
  uint8_t QR : 1;
  uint8_t RCode : 4;
  uint8_t Z : 3;
  uint8_t RA : 1;
  uint16_t QDCount;
  uint16_t ANCount;
  uint16_t NSCount;
  uint16_t ARCount;
};

struct DnsQuestion {
  uint16_t QType;
  uint16_t QClass;
};

struct DnsRecord {
  uint16_t Type;
  uint16_t Class;
  uint32_t TTL;
  uint16_t DataLength;
};
#pragma pack(pop)

DiagDnsServer::DiagDnsServer() {
  req_name_.reserve(64);
}

DiagDnsServer::~DiagDnsServer() { stopInternal(); }

void DiagDnsServer::downcaseAndRemoveWwwPrefix(String& name) {
  name.toLowerCase();
  name.replace("www.", "");
}

bool DiagDnsServer::start(uint16_t port, const String& domain_name,
                          const IPAddress& resolved) {
  stopInternal();
  port_ = port;
  String dn = domain_name;
  downcaseAndRemoveWwwPrefix(dn);
  if (domain_name == "*" || dn == "*") {
    dn = "";  // wildcard: accept any hostname
  }
  domain_name_ = dn;
  for (int i = 0; i < 4; ++i) resolved_ip_[i] = resolved[i];
  if (udp_.begin(port_) == 0) {
    return false;
  }
  return true;
}

void DiagDnsServer::stop() { stopInternal(); }

void DiagDnsServer::stopInternal() {
  udp_.stop();
  request_count_ = 0;
}

void DiagDnsServer::setErrorReplyCode(DiagDnsReplyCode code) {
  error_reply_code_ = code;
}

void DiagDnsServer::setTTL(uint32_t ttl_seconds) { ttl_ = ttl_seconds; }

void DiagDnsServer::processNextRequest() {
  current_packet_size_ = udp_.parsePacket();
  if (current_packet_size_ == 0) return;

  if (current_packet_size_ > static_cast<int>(kMaxDnsPacket)) {
    udp_.read(buffer_, kMaxDnsPacket);
    return;
  }

  const int read_len = udp_.read(buffer_, current_packet_size_);
  if (read_len <= 0) return;
  current_packet_size_ = read_len;
  if (current_packet_size_ < (int)sizeof(DnsHeader)) return;

  DnsHeader* dns_header = reinterpret_cast<DnsHeader*>(buffer_);

  uint8_t* query = buffer_ + sizeof(DnsHeader);
  uint16_t query_len = 0;
  while (query + query_len < buffer_ + current_packet_size_ &&
         query[query_len] != 0) {
    query_len += query[query_len] + 1;
  }
  if (query + query_len >= buffer_ + current_packet_size_) return;
  query_len++;
  if (query + query_len + sizeof(DnsQuestion) > buffer_ + current_packet_size_)
    return;

  // Extract the requested domain name
  req_name_.clear();
  uint8_t* p = query;
  while (p < query + query_len - 1) {
    uint8_t len = *p++;
    while (len-- && p < query + query_len - 1) {
      req_name_ += static_cast<char>(*p++);
    }
    if (len > 0) break;
    if (p < query + query_len - 1) req_name_ += '.';
  }
  downcaseAndRemoveWwwPrefix(req_name_);

  dns_header->QR = DNS_QR_RESPONSE;
  dns_header->QDCount = htons(1);
  dns_header->ANCount = htons(1);
  dns_header->NSCount = 0;
  dns_header->ARCount = 0;
  dns_header->RA = 1;

  if (error_reply_code_ != DiagDnsReplyCode::kNoError) {
    dns_header->RCode = static_cast<uint8_t>(error_reply_code_);
    replyWithCustomCode();
  } else if (!domain_name_.isEmpty() && req_name_ != domain_name_) {
    dns_header->RCode = static_cast<uint8_t>(DiagDnsReplyCode::kNonExistentDomain);
    replyWithCustomCode();
  } else {
    dns_header->RCode = static_cast<uint8_t>(DiagDnsReplyCode::kNoError);
    replyWithIP();
  }
#ifdef DEBUG_DNS
  if (kEnableVerboseSerialLogs) {
    LOGI("[DNSDBG] name=%s rcode=%u ip=%u.%u.%u.%u\n", req_name_.c_str(),
         static_cast<unsigned>(dns_header->RCode), resolved_ip_[0],
         resolved_ip_[1], resolved_ip_[2], resolved_ip_[3]);
  }
#endif
  ++request_count_;
  WifiDiagIncDns();
}

void DiagDnsServer::replyWithIP() {
  uint16_t ans_start = sizeof(DnsHeader);
  while (ans_start < current_packet_size_ && buffer_[ans_start] != 0) {
    ans_start += buffer_[ans_start] + 1;
  }
  ans_start++;  // null
  ans_start += sizeof(DnsQuestion);

  DnsRecord answer;
  answer.Type = htons(0x0001);   // A
  answer.Class = htons(0x0001);  // IN
  answer.TTL = htonl(ttl_);
  answer.DataLength = htons(4);

  uint16_t response_size = ans_start + sizeof(DnsRecord) + 4;
  if (response_size > kMaxDnsPacket) {
    return;
  }

  memcpy(buffer_ + ans_start, &answer, sizeof(DnsRecord));
  memcpy(buffer_ + ans_start + sizeof(DnsRecord), resolved_ip_, 4);

  udp_.beginPacket(udp_.remoteIP(), udp_.remotePort());
  udp_.write(buffer_, response_size);
  udp_.endPacket();
}

void DiagDnsServer::replyWithCustomCode() {
  udp_.beginPacket(udp_.remoteIP(), udp_.remotePort());
  udp_.write(buffer_, current_packet_size_);
  udp_.endPacket();
}
