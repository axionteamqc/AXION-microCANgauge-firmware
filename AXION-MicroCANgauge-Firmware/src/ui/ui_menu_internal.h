#pragma once

#include "ui_menu.h"

void HandleCanDiagnosticsAction(UiAction action, uint8_t& can_diag_page,
                                bool& can_diag_test_active,
                                uint32_t& can_diag_test_start_ms,
                                bool& request_exit,
                                UiMenu::MenuMode& mode,
                                UiMenu::DeviceSetupItem& device_item);

void RenderCanDiagnostics(OledU8g2& display, uint8_t viewport_y,
                          uint8_t viewport_h, bool send_buffer,
                          uint8_t can_diag_page, bool can_diag_test_active,
                          uint32_t can_diag_test_start_ms);
