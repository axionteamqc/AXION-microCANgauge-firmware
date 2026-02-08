#pragma once

#include <stdint.h>

#include "app_state.h"

void ResetBaroAuto();
void AutoAcquireBaro(AppState& state, uint32_t now_ms);
