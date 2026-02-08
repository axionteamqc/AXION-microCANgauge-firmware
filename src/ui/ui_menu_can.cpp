#include "ui/ui_menu_internal.h"

#include <Arduino.h>
#include <cstdio>

#include "app/app_globals.h"
#include "app/button_task.h"
#include "app/can_runtime.h"
#include "app/can_state_snapshot.h"
#include "can_rx.h"

namespace {

const char* TwaiStateStr(uint8_t st, bool passive_hint) {
  switch (st) {
    case 1:
      return passive_hint ? "PASS" : "RUN";
    case 2:
      return "BOFF";
    case 3:
      return "RECOV";
    case 0:
    default:
      return "STOP";
  }
}

void BytesToHex(const uint8_t* data, uint8_t len, char* out, size_t out_sz) {
  if (!out || out_sz == 0) return;
  size_t pos = 0;
  out[pos++] = 'D';
  if (pos < out_sz) out[pos++] = ':';
  const uint8_t capped = (len > 8) ? static_cast<uint8_t>(8) : len;
  for (uint8_t i = 0; i < capped && (pos + 2) < out_sz; ++i) {
    const int n = snprintf(&out[pos], out_sz - pos, "%02X",
                           static_cast<unsigned>(data[i]));
    if (n <= 0) break;
    pos += static_cast<size_t>(n);
    if (pos >= out_sz - 1) break;
  }
  if (pos >= out_sz) {
    out[out_sz - 1] = '\0';
  } else {
    out[pos] = '\0';
  }
}

}  // namespace

void HandleCanDiagnosticsAction(UiAction action, uint8_t& can_diag_page,
                                bool& can_diag_test_active,
                                uint32_t& can_diag_test_start_ms,
                                bool& request_exit,
                                UiMenu::MenuMode& mode,
                                UiMenu::DeviceSetupItem& device_item) {
  switch (action) {
    case UiAction::kClick2:
      if (can_diag_page == 3) {
        if (!can_diag_test_active) {
          can_diag_test_active = true;
          can_diag_test_start_ms = millis();
        } else {
          can_diag_test_active = false;
        }
      } else {
        mode = UiMenu::MenuMode::kDeviceSetup;
        device_item = UiMenu::DeviceSetupItem::kCanDiagnostics;
        can_diag_page = 0;
        can_diag_test_active = false;
      }
      break;
    case UiAction::kClick1:
      can_diag_page = static_cast<uint8_t>((can_diag_page + 1) % 4);
      break;
    case UiAction::kLong:
    case UiAction::kClick1Long:
      can_diag_page = static_cast<uint8_t>((can_diag_page + 4 - 1) % 4);
      break;
    case UiAction::kClick3:
      request_exit = true;
      break;
    default:
      break;
  }
}

void RenderCanDiagnostics(OledU8g2& display, uint8_t viewport_y,
                          uint8_t viewport_h, bool send_buffer,
                          uint8_t can_diag_page, bool can_diag_test_active,
                          uint32_t can_diag_test_start_ms) {
  display.simpleSetFontSmall();
  const uint8_t line_height = 10;
  uint8_t y = viewport_y + line_height;
  const uint8_t max_y =
      (viewport_h > 0) ? static_cast<uint8_t>(viewport_y + viewport_h) : 255;
  auto draw = [&](const char* s) {
    if (s && s[0] != '\0' && y < max_y) {
      display.simpleDrawStr(0, y, s);
      y = static_cast<uint8_t>(y + line_height);
    }
  };
  draw("CAN DIAGNOSTICS");
  char buf[32];
  static CanRxSnapshot s_snap{};
  static uint32_t s_snap_ms = 0;
  static CanRxCounters s_cnt{};
  const uint32_t now_ms = millis();
  if ((now_ms - s_snap_ms) > 50U) {  // ~20 Hz
    canrx_get_snapshot(s_snap);
    canrx_get_counters(s_cnt);
    s_snap_ms = now_ms;
  }
  CanStateSnapshot can_state{};
  GetCanStateSnapshot(can_state);
  uint32_t latest_ts = 0;
  uint8_t latest_idx = 255;
  for (uint8_t i = 0; i < 5; ++i) {
    if (s_snap.entries[i].valid && s_snap.entries[i].ts_ms >= latest_ts) {
      latest_ts = s_snap.entries[i].ts_ms;
      latest_idx = i;
    }
  }
  switch (can_diag_page) {
    case 0: {
      const bool ready = can_state.can_ready;
      const bool locked = can_state.can_bitrate_locked;
      const uint8_t tec = can_state.tec;
      const uint8_t rec = can_state.rec;
      const uint32_t age_ms =
          (latest_ts == 0) ? 0 : (now_ms - latest_ts);
      const bool passive = (tec >= 128U) || (rec >= 128U);
      const char* tw_state = TwaiStateStr(can_state.twai_state, passive);
      snprintf(buf, sizeof(buf), "CAN:%s LCK:%s TW:%s",
               ready ? "RDY" : "NO", locked ? "Y" : "N", tw_state);
      draw(buf);
      if (latest_ts == 0) {
        snprintf(buf, sizeof(buf), "P0 TEC:%u REC:%u AGE:--", tec, rec);
      } else {
        snprintf(buf, sizeof(buf), "P0 TEC:%u REC:%u AGE:%lums", tec, rec,
                 static_cast<unsigned long>(age_ms));
      }
      draw(buf);
      const uint32_t rx_age =
          (can_state.last_can_rx_ms == 0) ? 0xFFFFFFFFu
                                          : (now_ms - can_state.last_can_rx_ms);
      const uint32_t match_age =
          (can_state.last_can_match_ms == 0) ? 0xFFFFFFFFu
                                             : (now_ms - can_state.last_can_match_ms);
      if (rx_age == 0xFFFFFFFFu && match_age == 0xFFFFFFFFu) {
        snprintf(buf, sizeof(buf), "RXAGE:-- MAGE:--");
      } else {
        const unsigned long rx_val =
            (rx_age == 0xFFFFFFFFu) ? 0ul : static_cast<unsigned long>(rx_age);
        const unsigned long match_val =
            (match_age == 0xFFFFFFFFu) ? 0ul : static_cast<unsigned long>(match_age);
        snprintf(buf, sizeof(buf), "RXAGE:%lu MAGE:%lu", rx_val, match_val);
      }
      draw(buf);
      break;
    }
    case 1: {
      snprintf(buf, sizeof(buf), "RX:%.0f M:%.0f D:%.1f",
               static_cast<double>(can_state.can_rates.rx_per_s),
               static_cast<double>(can_state.can_rates.match_per_s),
               static_cast<double>(can_state.can_rates.drop_per_s));
      draw(buf);
      snprintf(buf, sizeof(buf), "OOB:%lu OVR:%lu MIS:%lu",
               static_cast<unsigned long>(can_state.can_stats.decode_oob),
               static_cast<unsigned long>(s_cnt.rx_overrun),
               static_cast<unsigned long>(can_state.can_stats.rx_missed));
      draw(buf);
      snprintf(buf, sizeof(buf), "BO:%lu ER:%lu TEC%u REC%u",
               static_cast<unsigned long>(can_state.can_stats.bus_off),
               static_cast<unsigned long>(can_state.can_stats.err_passive),
               static_cast<unsigned>(can_state.tec),
               static_cast<unsigned>(can_state.rec));
      draw(buf);
      snprintf(buf, sizeof(buf), "BTN:%lu UIW:%lu(%lums) OILW:%lu(%lums)",
               static_cast<unsigned long>(g_state.persist_audit.button_events),
               static_cast<unsigned long>(g_state.persist_audit.ui_writes),
               static_cast<unsigned long>(g_state.persist_audit.ui_write_max_ms),
               static_cast<unsigned long>(g_state.persist_audit.oil_writes),
               static_cast<unsigned long>(g_state.persist_audit.oil_write_max_ms));
      draw(buf);
      const uint32_t heap = ESP.getFreeHeap();
      const uint32_t btn_wm = ButtonTaskWatermark();
      const uint32_t can_wm = CanRxTaskWatermark();
      snprintf(buf, sizeof(buf), "HEAP:%lu BTNWM:%lu CANWM:%lu",
               static_cast<unsigned long>(heap),
               static_cast<unsigned long>(btn_wm),
               static_cast<unsigned long>(can_wm));
      draw(buf);
      break;
    }
    case 2: {
      if (latest_ts == 0 || latest_idx == 255) {
        draw("P2 ID:-- DL:-- A:--");
        draw("D:--");
        break;
      }
      const auto& e = s_snap.entries[latest_idx];
      const uint32_t age_ms = now_ms - e.ts_ms;
      snprintf(buf, sizeof(buf), "P2 ID:0x%03lX DL:%u A:%lums",
               static_cast<unsigned long>(e.id),
               static_cast<unsigned>(e.dlc),
               static_cast<unsigned long>(age_ms));
      draw(buf);
      char data_buf[24];
      BytesToHex(e.data, e.dlc, data_buf, sizeof(data_buf));
      draw(data_buf);
      break;
    }
    case 3: {
      bool active = can_diag_test_active;
      const uint32_t elapsed = active ? (now_ms - can_diag_test_start_ms) : 0;
      if (active && elapsed >= 60000U) {
        active = false;
      }
      if (active) {
        const uint32_t sec = elapsed / 1000U;
        snprintf(buf, sizeof(buf), "TEST RUN %lus", static_cast<unsigned long>(sec));
        draw(buf);
        snprintf(buf, sizeof(buf), "T:%lu D:%lu OVR:%lu",
                 static_cast<unsigned long>(s_cnt.rx_total),
                 static_cast<unsigned long>(s_cnt.rx_dash),
                 static_cast<unsigned long>(s_cnt.rx_overrun));
        draw(buf);
        if (latest_ts == 0) {
          snprintf(buf, sizeof(buf), "AGE:-- TEC:%u REC:%u",
                   static_cast<unsigned>(can_state.tec),
                   static_cast<unsigned>(can_state.rec));
        } else {
          snprintf(buf, sizeof(buf), "AGE:%lums TEC:%u REC:%u",
                   static_cast<unsigned long>(now_ms - latest_ts),
                   static_cast<unsigned>(can_state.tec),
                   static_cast<unsigned>(can_state.rec));
        }
        draw(buf);
      } else {
        draw("TEST RX STRESS");
        if (can_diag_test_active) {
          draw("DONE (Click2 reset)");
        } else {
          draw("Click2 START (60s)");
        }
      }
      break;
    }
    default:
      break;
  }
  if (send_buffer) {
    display.simpleSend();
  }
}
