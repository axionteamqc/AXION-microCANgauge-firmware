#include "settings/ui_persist_build.h"

UiPersist BuildUiPersistFromState(const AppState& state) {
  UiPersist ui{};
  // Zone pages (runtime selection).
  ui.zone_page[0] = state.page_index[0];
  ui.zone_page[1] = state.page_index[1];
  ui.zone_page[2] = state.page_index[2];
  // Boot pages.
  ui.boot_page[0] = state.boot_page_index[0];
  ui.boot_page[1] = state.boot_page_index[1];
  ui.boot_page[2] = state.boot_page_index[2];
  // Legacy compatibility pages.
  const bool has_secondary =
      (state.display_topology == DisplayTopology::kDualSmall) ||
      (state.display_topology == DisplayTopology::kLargePlusSmall);
  ui.page0 = ui.zone_page[0];
  ui.page1 = has_secondary ? ui.zone_page[2] : ui.zone_page[1];
  // Focus / topology / flips.
  ui.focus = state.focus_zone;
  ui.display_topology = static_cast<uint8_t>(state.display_topology);
  ui.flip0 = state.screen_cfg[0].flip_180;
  ui.flip1 = state.screen_cfg[1].flip_180;
  ui.demo_mode = state.demo_mode;
  strlcpy(ui.ecu_type, state.ecu_type, sizeof(ui.ecu_type));
  // Units / alerts masks.
  ui.page_units_mask = state.page_units_mask;
  ui.page_alert_max_mask = state.page_alert_max_mask;
  ui.page_alert_min_mask = state.page_alert_min_mask;
  ui.page_hidden_mask = state.page_hidden_mask;
  // Deprecated fields left default (units0/1) for backward compatibility.
  return ui;
}
