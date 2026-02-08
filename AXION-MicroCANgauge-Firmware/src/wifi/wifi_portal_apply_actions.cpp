#include "wifi/wifi_portal_apply_internal.h"

#include <Arduino.h>
#include <WebServer.h>

#include "app/app_globals.h"
#include "app/app_runtime.h"
#include "app/app_sleep.h"
#include "app_state.h"

void HandleApplyResetExtrema(WebServer& server, uint32_t& form_nonce) {
  portENTER_CRITICAL(&g_state_mux);
  for (size_t i = 0; i < kPageCount; ++i) {
    g_state.page_recorded_min[i] = NAN;
    g_state.page_recorded_max[i] = NAN;
  }
  resetAllMax(g_state, millis());
  portEXIT_CRITICAL(&g_state_mux);
  form_nonce = static_cast<uint32_t>(millis() ^ random(0xFFFFFFFF));
  server.send(200, "text/html",
              "<html><body><h1>OK</h1><p>Extrema cleared.</p></body></html>");
  AppSleepMs(100);
  ESP.restart();
}

void HandleApplyFactoryReset(WebServer& server, uint32_t& form_nonce) {
  g_nvs.factoryResetClearAll();
  form_nonce = static_cast<uint32_t>(millis() ^ random(0xFFFFFFFF));
  server.send(200, "text/plain",
              "Factory reset, rebooting... (Wi-Fi password resets to default)");
  AppSleepMs(100);
  ESP.restart();
}

void HandleApplyResetBaro(WebServer& server, uint32_t& form_nonce) {
  ResetBaroPersist();
  form_nonce = static_cast<uint32_t>(millis() ^ random(0xFFFFFFFF));
  server.send(200, "text/plain", "BARO reset, rebooting...");
  AppSleepMs(100);
  ESP.restart();
}
