#pragma once

#include "alerts/alerts_engine.h"
#include "app_state.h"
#include "drivers/oled_u8g2.h"
#include "data/datastore.h"
#include "ui/pages.h"

void resetMaxForFocusPage(AppState& state, uint32_t now_ms);
void renderUi(AppState& state, const DataStore& store,
              OledU8g2& oled_primary, OledU8g2& oled_secondary,
              uint32_t now_ms, bool allow_oled1, bool allow_oled2,
              const AlertsEngine& alerts);
