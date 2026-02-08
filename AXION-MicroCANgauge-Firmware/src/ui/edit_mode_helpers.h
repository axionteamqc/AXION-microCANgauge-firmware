#pragma once

#include "ui/pages.h"

uint8_t currentPageIndex(const AppState& state, uint8_t screen);
PageId currentPageId(const AppState& state, uint8_t screen);
void persistThresholds(const AppState& state);
void enterEditMode(AppState& state, uint8_t screen, const DataStore& store,
                   uint32_t now_ms);
void exitEditMode(AppState& state, uint8_t screen);
void saveEdit(AppState& state, uint8_t screen);
