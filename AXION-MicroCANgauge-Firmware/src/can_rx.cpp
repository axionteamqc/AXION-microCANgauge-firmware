#include "can_rx.h"

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "app/app_globals.h"

namespace {

constexpr uint32_t kIds[] = {0x5E8, 0x5E9, 0x5EA, 0x5EB, 0x5EC};

struct StoreEntry {
  uint8_t dlc = 0;
  uint8_t data[8] = {0};
  uint32_t ts_ms = 0;
  bool valid = false;
};

StoreEntry g_store[sizeof(kIds) / sizeof(kIds[0])];
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
struct Counters {
  uint32_t rx_total = 0;
  uint32_t rx_match = 0;
  uint32_t rx_dash = 0;
  uint32_t rx_overrun = 0;
  uint32_t rx_timeout = 0;
  uint32_t rx_err_passive = 0;
  uint32_t decode_oob = 0;
} g_counters;
TaskHandle_t g_task = nullptr;

int indexForId(uint32_t id) {
  for (size_t i = 0; i < sizeof(kIds) / sizeof(kIds[0]); ++i) {
    if (kIds[i] == id) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

void canrx_init() {
  portENTER_CRITICAL(&g_mux);
  for (auto& e : g_store) {
    e.dlc = 0;
    memset(e.data, 0, sizeof(e.data));
    e.ts_ms = 0;
    e.valid = false;
  }
  g_counters = Counters{};
  portEXIT_CRITICAL(&g_mux);
}

static void canrx_task(void* arg) {
  (void)arg;
  twai_message_t msg{};
  uint32_t prev_rx_missed = 0;
  for (;;) {
    if (!g_twai.isStarted()) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (g_twai.receive(msg, pdMS_TO_TICKS(20))) {
      const uint32_t now_ms = millis();
      canrx_record(msg, now_ms);
      portENTER_CRITICAL(&g_mux);
      ++g_counters.rx_total;
      const int idx = indexForId(msg.identifier);
      if (idx >= 0) {
        ++g_counters.rx_match;
        ++g_counters.rx_dash;
      }
      portEXIT_CRITICAL(&g_mux);
    } else {
      // timeout path (no frame)
      portENTER_CRITICAL(&g_mux);
      ++g_counters.rx_timeout;
      portEXIT_CRITICAL(&g_mux);
      // poll alerts for overrun
      uint32_t alerts = 0;
      while (g_twai.readAlerts(alerts, 0)) {
        if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
          portENTER_CRITICAL(&g_mux);
          ++g_counters.rx_overrun;
          portEXIT_CRITICAL(&g_mux);
        }
        if (alerts & TWAI_ALERT_ERR_PASS) {
          portENTER_CRITICAL(&g_mux);
          ++g_counters.rx_err_passive;
          portEXIT_CRITICAL(&g_mux);
        }
      }
      twai_status_info_t st{};
      if (twai_get_status_info(&st) == ESP_OK) {
        if (st.rx_missed_count > prev_rx_missed) {
          const uint32_t delta = st.rx_missed_count - prev_rx_missed;
          portENTER_CRITICAL(&g_mux);
          g_counters.rx_overrun += delta;
          portEXIT_CRITICAL(&g_mux);
        }
        prev_rx_missed = st.rx_missed_count;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void canrx_start() {
  if (g_task != nullptr) return;
  xTaskCreatePinnedToCore(canrx_task, "canrx", 2048, nullptr,
                          configMAX_PRIORITIES - 1, &g_task, 0);
}

void canrx_record(const twai_message_t& msg, uint32_t now_ms) {
  const int idx = indexForId(msg.identifier);
  if (idx < 0) return;
  const uint8_t dlc = (msg.data_length_code > 8) ? static_cast<uint8_t>(8)
                                                : msg.data_length_code;
  portENTER_CRITICAL(&g_mux);
  StoreEntry& e = g_store[idx];
  e.dlc = dlc;
  memcpy(e.data, msg.data, dlc);
  if (dlc < sizeof(e.data)) {
    memset(e.data + dlc, 0, sizeof(e.data) - dlc);
  }
  e.ts_ms = now_ms;
  e.valid = true;
  portEXIT_CRITICAL(&g_mux);
}

void canrx_get_snapshot(CanRxSnapshot& out) {
  portENTER_CRITICAL(&g_mux);
  for (size_t i = 0; i < sizeof(kIds) / sizeof(kIds[0]); ++i) {
    out.entries[i].id = kIds[i];
    out.entries[i].dlc = g_store[i].dlc;
    memcpy(out.entries[i].data, g_store[i].data, sizeof(out.entries[i].data));
    out.entries[i].ts_ms = g_store[i].ts_ms;
    out.entries[i].valid = g_store[i].valid;
  }
  portEXIT_CRITICAL(&g_mux);
}

void canrx_get_counters(CanRxCounters& out) {
  portENTER_CRITICAL(&g_mux);
  out.rx_total = g_counters.rx_total;
  out.rx_match = g_counters.rx_match;
  out.rx_dash = g_counters.rx_dash;
  out.rx_overrun = g_counters.rx_overrun;
  out.rx_timeout = g_counters.rx_timeout;
  out.rx_err_passive = g_counters.rx_err_passive;
  out.decode_oob = g_counters.decode_oob;
  portEXIT_CRITICAL(&g_mux);
}
