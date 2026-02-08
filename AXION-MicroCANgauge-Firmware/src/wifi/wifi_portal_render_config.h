#pragma once

#include "app/app_ui_snapshot.h"
#include "ui/pages.h"
#include "wifi/wifi_portal_http.h"

using SendFn = PortalWriter;

void renderDownloads(const SendFn& send);
void renderSetupSnapshot(const SendFn& send, size_t page_count,
                         const AppUiSnapshot& ui);
void renderBootSelect(const SendFn& send, size_t page_count, uint8_t current,
                      const char* name, const char* label, const char* sel_id,
                      const char* hidden_id);
void renderPerPageTable(const SendFn& send, size_t page_count,
                        const PageDef* pages, const AppUiSnapshot& ui);
void renderBootTextInputs(const SendFn& send);
void renderActions(const SendFn& send);
void renderRecordedExtrema(const SendFn& send, size_t page_count,
                           const PageDef* pages, const AppUiSnapshot& ui);
