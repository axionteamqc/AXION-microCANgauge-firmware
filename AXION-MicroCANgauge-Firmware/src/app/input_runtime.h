// Button event processing extracted from the main runtime.
#pragma once

#include <Arduino.h>
#include "ui_menu.h"

struct BtnMsg {
  UiAction action;
  uint32_t ts_ms;
};

// Drain button queue and apply UI actions.
void InputRuntimeTick(uint32_t now_ms);
