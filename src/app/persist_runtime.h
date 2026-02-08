#pragma once

#include <Arduino.h>

void markUiDirty(uint32_t now_ms);
void PersistRequestUiSave();
struct OilPersist;
void PersistRequestOilSave(const OilPersist& op);
void PersistRuntimeTick(uint32_t now_ms);
