#include "app/can_state_snapshot.h"

#include "app/app_globals.h"
#include "freertos/portmacro.h"

void GetCanStateSnapshot(CanStateSnapshot& out) {
  portENTER_CRITICAL(&g_state_mux);
  out.can_ready = g_state.can_ready;
  out.can_bitrate_locked = g_state.can_bitrate_locked;
  out.last_can_rx_ms = g_state.last_can_rx_ms;
  out.last_can_match_ms = g_state.last_can_match_ms;
  out.twai_state = g_state.twai_state;
  out.tec = g_state.tec;
  out.rec = g_state.rec;
  out.can_stats = g_state.can_stats;
  out.can_rates = g_state.can_rates;
  out.last_bus_off_ms = g_state.last_bus_off_ms;
  portEXIT_CRITICAL(&g_state_mux);
}
