#pragma once

#include <Arduino.h>
#include "app_config.h"
#include "drivers/oled_u8g2.h"
#include "app_state.h"
#include "can_link/twai_link.h"
#include "ms3_decode/ms3_decode.h"
#include "data/datastore.h"
#include "settings/nvs_store.h"
#include "freertos/portmacro.h"
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#include <HWCDC.h>
// Route Serial logs to the native USB CDC/JTAG port on ESP32-C3.
#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 1)
#ifndef Serial
#define Serial USBSerial
#endif
#endif
#endif
#if SETUP_WIZARD_ENABLED
#include "setup_wizard/setup_wizard.h"
#endif
#include "alerts/alerts_engine.h"
#include "ecu/ecu_manager.h"

extern OledU8g2 g_oled_primary;
extern OledU8g2 g_oled_secondary;
extern QueueHandle_t g_btnQueue;
extern AppState g_state;
extern TwaiLink g_twai;
extern Ms3Decoder g_decoder;
extern DataStore g_datastore_can;
extern DataStore g_datastore_demo;
extern volatile uint32_t g_can_rx_edge_count;
extern portMUX_TYPE g_state_mux;
extern uint8_t g_wire_sda_pin;
extern uint8_t g_wire_scl_pin;
DataStore& ActiveStore();
extern NvsStore g_nvs;
#if SETUP_WIZARD_ENABLED
extern SetupWizard g_setup_wizard;
#endif
extern AlertsEngine g_alerts;
extern EcuManager g_ecu_mgr;
