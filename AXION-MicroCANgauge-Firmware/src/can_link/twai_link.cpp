#include "can_link/twai_link.h"
#include <Arduino.h>
#include "config/logging.h"
#include "pins.h"

TwaiLink::TwaiLink()
    : started_(false),
      tx_app_enabled_(false),
      tx_warned_(false),
      current_bitrate_(0),
      current_mode_(TWAI_MODE_NORMAL) {}

bool TwaiLink::startListenOnly(uint32_t bitrate) {
  return startWithMode(bitrate, TWAI_MODE_LISTEN_ONLY);
}

bool TwaiLink::startNormal(uint32_t bitrate) {
  return startWithMode(bitrate, TWAI_MODE_NORMAL);
}

void TwaiLink::setTxAppEnabled(bool on) {
  tx_app_enabled_ = on;
}

bool TwaiLink::transmit(const twai_message_t& msg, TickType_t timeout_ticks) {
  if (!started_) {
    return false;
  }
  if (!tx_app_enabled_) {
    if (!tx_warned_) {
      LOGV("CAN TX disabled (app)\n");
      tx_warned_ = true;
    }
    return false;
  }
  return twai_transmit(&msg, timeout_ticks) == ESP_OK;
}

bool TwaiLink::startWithMode(uint32_t bitrate, twai_mode_t mode) {
  if (started_) {
    if (bitrate == current_bitrate_ && mode == current_mode_) {
      return true;
    }
    twai_stop();
    twai_driver_uninstall();
    started_ = false;
  }

  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  bool supported = true;
  switch (bitrate) {
    case 1000000:
      t_config = TWAI_TIMING_CONFIG_1MBITS();
      break;
    case 500000:
      t_config = TWAI_TIMING_CONFIG_500KBITS();
      break;
    case 250000:
      t_config = TWAI_TIMING_CONFIG_250KBITS();
      break;
    case 125000:
      t_config = TWAI_TIMING_CONFIG_125KBITS();
      break;
    case 100000:
      t_config = TWAI_TIMING_CONFIG_100KBITS();
      break;
    case 50000:
      t_config = TWAI_TIMING_CONFIG_50KBITS();
      break;
    case 25000:
      t_config = TWAI_TIMING_CONFIG_25KBITS();
      break;
    default:
      supported = false;
      break;
  }

  if (!supported) {
    LOGE("TWAI unsupported bitrate: %lu\r\n",
         static_cast<unsigned long>(bitrate));
    return false;
  }

  twai_general_config_t g_config = {};
  g_config.mode = mode;
  g_config.tx_io = static_cast<gpio_num_t>(Pins::kCanTx);
  g_config.rx_io = static_cast<gpio_num_t>(Pins::kCanRx);
  g_config.clkout_io = TWAI_IO_UNUSED;
  g_config.bus_off_io = TWAI_IO_UNUSED;
  g_config.tx_queue_len = 1;  // minimal queue; TX app remains gated off
  g_config.rx_queue_len = 96;
  g_config.alerts_enabled = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS |
                            TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_QUEUE_FULL;
  g_config.clkout_divider = 0;

  // Acceptance filter: standard IDs only, centered on MS3 dash frames 0x5E8..0x5EC.
  // Hardware mask granularity forces a 0x5E8..0x5EF window (lower 3 bits wildcard).
  // Software decode still rejects any extra IDs outside the expected profile.
  const uint32_t id_base = 0x5E8;
  const uint32_t id_mask = 0x7F8;  // compare upper 8 bits, ignore lower 3
  twai_filter_config_t f_config = {};
  f_config.acceptance_code = (id_base << 21);      // standard ID placement
  f_config.acceptance_mask = ~(id_mask << 21);     // 0=compare, 1=don't care
  f_config.single_filter = true;

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    return false;
  }
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }
  started_ = true;
  current_bitrate_ = bitrate;
  current_mode_ = mode;
  return true;
}

bool TwaiLink::stop() {
  if (!started_) {
    return true;
  }
  const esp_err_t res = twai_stop();
  started_ = false;
  pinMode(Pins::kCanTx, INPUT_PULLUP);
  return res == ESP_OK;
}

bool TwaiLink::uninstall() {
  if (started_) {
    twai_stop();
    started_ = false;
  }
  const esp_err_t res = twai_driver_uninstall();
  pinMode(Pins::kCanTx, INPUT_PULLUP);
  current_bitrate_ = 0;
  return res == ESP_OK;
}

bool TwaiLink::isStarted() const {
  return started_;
}

bool TwaiLink::receive(twai_message_t& msg, TickType_t timeout_ticks) {
  if (!started_) {
    return false;
  }
  return twai_receive(&msg, timeout_ticks) == ESP_OK;
}

bool TwaiLink::readAlerts(uint32_t& alerts, TickType_t timeout_ticks) {
  if (!started_) {
    return false;
  }
  return twai_read_alerts(&alerts, timeout_ticks) == ESP_OK;
}

bool TwaiLink::isNormalMode() const {
  return started_ && current_mode_ == TWAI_MODE_NORMAL;
}
