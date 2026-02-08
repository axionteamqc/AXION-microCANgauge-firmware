#include <Arduino.h>
#include <Wire.h>
#include <cstring>
#include <cmath>

#include "alerts/alerts_engine.h"
#include "app/app_globals.h"
#include "app/app_runtime.h"
#include "app_config.h"
#include "app_state.h"
#include "boot_ui.h"
#include "can_link/can_autobaud.h"
#include "can_link/twai_link.h"
#include "data/datastore.h"
#include "drivers/oled_u8g2.h"
#include "ecu/ecu_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "app/persist_runtime.h"
#include "ms3_decode/ms3_decode.h"
#include "pins.h"
#include "settings/nvs_store.h"
#if SETUP_WIZARD_ENABLED
#include "setup_wizard/setup_wizard.h"
#endif
#include "ui_render.h"

OledU8g2 g_oled_primary(OledU8g2::Bus::kHw, Pins::kI2cScl, Pins::kI2cSda,
                        AppConfig::kOledResetPin);
OledU8g2 g_oled_secondary(OledU8g2::Bus::kSw, Pins::kI2c2Scl, Pins::kI2c2Sda,
                          AppConfig::kOledResetPin);
QueueHandle_t g_btnQueue = nullptr;
AppState g_state;
TwaiLink g_twai;
Ms3Decoder g_decoder;
DataStore g_datastore_can;
DataStore g_datastore_demo;
NvsStore g_nvs;
#if SETUP_WIZARD_ENABLED
SetupWizard g_setup_wizard(g_twai, g_datastore_can, g_nvs);
#endif
AlertsEngine g_alerts;
EcuManager g_ecu_mgr;

#ifndef WIFI_PORTAL_DIAG_ONLY
#define WIFI_PORTAL_DIAG_ONLY 0
#endif

#if WIFI_PORTAL_DIAG_ONLY
void DiagOnlySetup();
void DiagOnlyLoop();

void setup() { DiagOnlySetup(); }
void loop() { DiagOnlyLoop(); }
#else
void setup() { AppSetup(); }
void loop() { AppLoopTick(); }
#endif
