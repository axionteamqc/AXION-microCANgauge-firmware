#pragma once

#include <Arduino.h>
#include <driver/twai.h>

#include "pins.h"

class TwaiLink {
 public:
  TwaiLink();
  bool startListenOnly(uint32_t bitrate);
  bool startNormal(uint32_t bitrate);
  // Application TX is gated and disabled by default.
  void setTxAppEnabled(bool on);
  bool transmit(const twai_message_t& msg, TickType_t timeout_ticks);
  bool stop();
  bool uninstall();
  bool isStarted() const;
  bool isNormalMode() const;
  bool receive(twai_message_t& msg, TickType_t timeout_ticks);
  bool readAlerts(uint32_t& alerts, TickType_t timeout_ticks);

 private:
  bool startWithMode(uint32_t bitrate, twai_mode_t mode);
  bool started_;
  bool tx_app_enabled_;
  bool tx_warned_;
  uint32_t current_bitrate_;
  twai_mode_t current_mode_;
};
