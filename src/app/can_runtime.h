// CAN receive/dispatch loop extracted from the main runtime.
#pragma once

#include <Arduino.h>

// Tick CAN handling: receive frames, decode, update datastore, poll alerts.
void CanRuntimeTick(uint32_t now_ms);
void StartCanRxTask();
uint32_t CanRxTaskWatermark();
