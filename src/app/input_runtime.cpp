#include "app/input_runtime.h"

#include <Arduino.h>

#include "app/app_globals.h"
#include "app/app_sleep.h"
#include "app/persist_runtime.h"
#include "app_state.h"
#include "app/page_mask.h"
#include "data/datastore.h"
#include "config/logging.h"
#include "ui_render.h"
#include "ui_menu.h"
#include "ui/pages.h"
#include "ui/edit_mode_helpers.h"
#include "wifi/wifi_portal.h"
#include "settings/ui_persist_build.h"

// g_state ownership summary:
// - CAN RX task writes CAN stats/state under g_state_mux.
// - UI loop (this file) writes UI/navigation fields; lock only when fields are shared.
// - Wi-Fi portal applies config and writes shared fields under g_state_mux.
// Rule: any field read across tasks must be written under g_state_mux.

void resetAllMax(AppState& state, uint32_t now_ms);
void resetMaxForFocusPage(AppState& state, uint32_t now_ms);

template <typename F>
static inline void WithStateLock(F&& fn) {
  portENTER_CRITICAL(&g_state_mux);
  fn();
  portEXIT_CRITICAL(&g_state_mux);
}

void InputRuntimeTick(uint32_t now_ms) {
  BtnMsg msg;
  while (g_btnQueue && xQueueReceive(g_btnQueue, &msg, 0) == pdTRUE) {
    ++g_state.persist_audit.button_events;
    const UiAction action = msg.action;
    WithStateLock([&]() {
      g_state.last_input_ms = msg.ts_ms;
    });
    const uint8_t focus = g_state.focus_zone;
    const uint8_t zone_count = GetActiveZoneCount(g_state);
#if SETUP_WIZARD_ENABLED
    if (g_setup_wizard.isActive()) {
      g_setup_wizard.handleAction(action, g_state);
      // Force an immediate refresh so every button press yields feedback.
      renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary, now_ms,
               true, true, g_alerts);
      continue;
    }
#endif
    auto forceAll = [&]() {
      for (uint8_t z = 0; z < zone_count && z < kMaxZones; ++z) {
        g_state.force_redraw[z] = true;
      }
    };
    auto zone_active = [&](uint8_t z) -> bool {
      switch (g_state.display_topology) {
        case DisplayTopology::kSmallOnly:
          return z == 0;
        case DisplayTopology::kDualSmall:
          if (z == 0) return true;
          if (z == 2) return g_state.dual_screens;
          return false;
        case DisplayTopology::kLargeOnly:
          return z == 0 || z == 1;
        case DisplayTopology::kLargePlusSmall:
          if (z == 0 || z == 1) return true;
          if (z == 2) return g_state.dual_screens;
          return false;
        case DisplayTopology::kUnconfigured:
        default:
          return z == 0;
      }
      return false;
    };
  auto setLockToast = [&](AppState::LockToast type, uint32_t until_ms) {
    for (uint8_t z = 0; z < kMaxZones; ++z) {
      if (!zone_active(z)) continue;
      g_state.lock_toast_type[z] = type;
      g_state.lock_toast_until_ms[z] = until_ms;
      g_state.force_redraw[z] = true;
    }
  };
    auto enterWifiMode = [&](void) {
      WithStateLock([&]() {
        g_state.wifi_mode_active = true;
        g_state.wifi_exit_confirm = false;
      });
      WifiPortalEnter();
      g_oled_secondary.simpleClear();
      g_oled_secondary.simpleSend();
      g_oled_secondary.setSleep(true);
      forceAll();
      renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
               now_ms, true, true, g_alerts);
    };

    if (g_state.wifi_mode_active) {
      if (action == UiAction::kLong || action == UiAction::kClick1Long) {
        if (!g_state.wifi_exit_confirm) {
          WithStateLock([&]() {
            g_state.wifi_exit_confirm = true;
            g_state.wifi_exit_confirm_until_ms = now_ms + 5000;
          });
        } else {
          WithStateLock([&]() {
            g_state.wifi_mode_active = false;
            g_state.wifi_exit_confirm = false;
          });
          WifiPortalExit();
          g_oled_secondary.setSleep(false);
          g_oled_secondary.simpleClear();
          g_oled_secondary.simpleSend();
          forceAll();
          renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                   now_ms, true, true, g_alerts);
        }
      } else if (action == UiAction::kClick1 || action == UiAction::kClick2 ||
                 action == UiAction::kClick3 || action == UiAction::kClick4 ||
                 action == UiAction::kClick5) {
        WithStateLock([&]() {
          g_state.wifi_exit_confirm = false;
        });
      }
      if (g_state.wifi_exit_confirm &&
          now_ms > g_state.wifi_exit_confirm_until_ms) {
        WithStateLock([&]() {
          g_state.wifi_exit_confirm = false;
        });
      }
      continue;
    }

    // Display topology setup when unconfigured
    if (g_state.display_topology == DisplayTopology::kUnconfigured) {
      static const DisplayTopology kOptions[] = {
          DisplayTopology::kSmallOnly, DisplayTopology::kDualSmall,
          DisplayTopology::kLargePlusSmall, DisplayTopology::kLargeOnly};
      const uint8_t option_count =
          static_cast<uint8_t>(sizeof(kOptions) / sizeof(kOptions[0]));
      if (action == UiAction::kClick1) {
        WithStateLock([&]() {
          g_state.display_setup_index = static_cast<uint8_t>(
              (g_state.display_setup_index + 1) % option_count);
        });
      } else if (action == UiAction::kLong) {
        WithStateLock([&]() {
          g_state.display_setup_index = static_cast<uint8_t>(
              (g_state.display_setup_index + option_count - 1) % option_count);
        });
      } else if (action == UiAction::kClick2) {
        DisplayTopology chosen =
            kOptions[g_state.display_setup_index % option_count];
        WithStateLock([&]() {
          g_state.display_topology = chosen;
        });
        UiPersist ui{};
        ui.zone_page[0] = g_state.page_index[0];
        ui.zone_page[1] = g_state.page_index[1];
        ui.zone_page[2] = g_state.page_index[1];
        ui.page0 = ui.zone_page[0];
        ui.page1 = ui.zone_page[2];
        ui.focus = g_state.focus_zone;
        ui.display_topology = static_cast<uint8_t>(chosen);
        ui.flip0 = g_state.screen_cfg[0].flip_180;
        ui.flip1 = g_state.screen_cfg[1].flip_180;
        ui.page_units_mask = g_state.page_units_mask;
        ui.page_alert_max_mask = g_state.page_alert_max_mask;
        ui.page_alert_min_mask = g_state.page_alert_min_mask;
        g_nvs.saveUiPersist(ui);
        if (g_state.oled_primary_ready) {
          g_oled_primary.drawLines("SAVED", "REBOOTING", nullptr, nullptr);
        }
        AppSleepMs(300);
        ESP.restart();
      }
      g_state.force_redraw[0] = true;
      continue;
    }

    if (action == UiAction::kLockGesture) {
      WithStateLock([&]() {
        g_state.ui_locked = true;
      });
      setLockToast(AppState::LockToast::kLocked, now_ms + 1500);
      continue;
    }
    if (action == UiAction::kUnlockGesture) {
      WithStateLock([&]() {
        g_state.ui_locked = false;
      });
      setLockToast(AppState::LockToast::kUnlocked, now_ms + 1500);
      continue;
    }
    if (g_state.ui_locked) {
      if (action != UiAction::kNone) {
        setLockToast(AppState::LockToast::kBlocked, now_ms + 500);
      }
      continue;
    }

    // Edit mode handling
    if (g_state.edit_mode.mode[g_state.focus_zone] != EditModeState::Mode::kNone) {
      const uint8_t focus = g_state.focus_zone;
      g_state.edit_mode.last_activity_ms[focus] = now_ms;
      const PageId pid = currentPageId(g_state, focus);
      ThresholdGrid grid{};
      const bool has_grid = GetThresholdGridWithState(
          pid, g_state, DisplayConfigForZone(g_state, focus).imperial_units,
          grid);
      const PageMeta* meta = FindPageMeta(pid);
      const float step = has_grid ? grid.step_disp : 1.0f;

      switch (action) {
        case UiAction::kClick1:
          if (has_grid) {
            g_state.edit_mode.display_value[focus] =
                SnapToGrid(g_state.edit_mode.display_value[focus], grid);
            g_state.edit_mode.display_value[focus] =
                SnapToGrid(g_state.edit_mode.display_value[focus] + step, grid);
          } else {
            g_state.edit_mode.display_value[focus] += step;
          }
          g_state.force_redraw[focus] = true;
          break;
        case UiAction::kLong:
          if (has_grid) {
            g_state.edit_mode.display_value[focus] =
                SnapToGrid(g_state.edit_mode.display_value[focus], grid);
            g_state.edit_mode.display_value[focus] =
                SnapToGrid(g_state.edit_mode.display_value[focus] - step, grid);
          } else {
            g_state.edit_mode.display_value[focus] -= step;
          }
          g_state.allow_oled2_during_hold = true;
          forceAll();
          break;
        case UiAction::kClick2:
          if (g_state.edit_mode.locked_maxmin[focus]) {
            saveEdit(g_state, focus);
          } else if (meta->has_min && meta->has_max) {
            if (g_state.edit_mode.mode[focus] == EditModeState::Mode::kEditMax) {
              g_state.edit_mode.mode[focus] = EditModeState::Mode::kEditMin;
            } else if (g_state.edit_mode.mode[focus] == EditModeState::Mode::kEditMin) {
              g_state.edit_mode.mode[focus] = EditModeState::Mode::kEditMax;
            }
            g_state.force_redraw[focus] = true;
          }
          break;
        case UiAction::kClick4:
          saveEdit(g_state, focus);
          break;
        case UiAction::kClick3:
          if (g_state.edit_mode.locked_maxmin[focus]) {
            saveEdit(g_state, focus);
          } else {
            exitEditMode(g_state, focus);
          }
          break;
        default:
          break;
      }
      continue;
    }

    // Extrema view handling
    if (g_state.extrema_view.active[g_state.focus_zone]) {
      const uint8_t focus_zone = g_state.focus_zone;
      switch (action) {
        case UiAction::kClick1:
          g_state.extrema_view.show_min[focus_zone] =
              !g_state.extrema_view.show_min[focus_zone];
          g_state.force_redraw[focus_zone] = true;
          break;
        case UiAction::kLong:
        case UiAction::kClick3:
        case UiAction::kClick4:
        case UiAction::kClick5:
        case UiAction::kClick1Long:
        case UiAction::kClick2:
          g_state.extrema_view.active[focus_zone] = false;
          g_state.force_redraw[focus_zone] = true;
          break;
        default:
          break;
      }
      continue;
    }

    // Menu handling priority
    if (g_state.ui_menu.isActive()) {
      if (action == UiAction::kClick3) {
        g_state.ui_menu.exit();
        forceAll();
        renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                 now_ms, true, true, g_alerts);
      } else if (action == UiAction::kClick1 || action == UiAction::kLong ||
                 action == UiAction::kClick2 || action == UiAction::kClick1Long) {
        const uint8_t cur_page = currentPageIndex(g_state, focus);
        const PageMeta* meta = FindPageMeta(currentPageId(g_state, focus));
        const ValueKind page_kind =
            meta ? meta->kind : ValueKind::kNone;
        bool prev_flip = DisplayConfigForZone(g_state, focus).flip_180;
        bool page_units = GetPageUnits(g_state, cur_page);
        bool prev_page_units = page_units;
        bool max_alert_enabled = GetPageMaxAlertEnabled(g_state, cur_page);
        bool prev_max_alert_enabled = max_alert_enabled;
        bool min_alert_enabled = GetPageMinAlertEnabled(g_state, cur_page);
        bool prev_min_alert_enabled = min_alert_enabled;
        bool start_setup = false;
        bool factory_reset = false;
        bool display_mode_changed = false;
        DisplayTopology topo = g_state.display_topology;
        DisplayTopology prev_topo = topo;
        bool dummy_rerun = false;
        bool save_screens = false;
        bool start_edit_max = false;
        bool start_edit_min = false;
        bool start_wifi_mode = false;
        const bool prev_oil_swap =
            (g_state.user_sensor[0].source == UserSensorSource::kSensor2) &&
            (g_state.user_sensor[1].source == UserSensorSource::kSensor1);
        bool toggle_hide_page = false;
        bool unhide_all_pages = false;
        bool reset_baro_request = false;
        const bool prev_flip0 = g_state.screen_cfg[0].flip_180;
        const bool prev_flip1 = g_state.screen_cfg[1].flip_180;
        g_state.ui_menu.handleAction(action, DisplayConfigForZone(g_state, focus),
                                     g_state.reset_all_max_request, start_setup,
                                     page_kind, page_units, factory_reset,
                                     max_alert_enabled, min_alert_enabled, topo,
                                     save_screens, display_mode_changed, dummy_rerun,
                                     start_edit_max, start_edit_min, start_wifi_mode,
                                     toggle_hide_page, unhide_all_pages,
                                     reset_baro_request);
        if (display_mode_changed) {
          if (topo != prev_topo) {
            WithStateLock([&]() {
              g_state.display_topology = topo;
            });
          }
          UiPersist ui{};
          ui.zone_page[0] = g_state.page_index[0];
          ui.zone_page[1] = g_state.page_index[1];
          ui.zone_page[2] = g_state.page_index[2];
          ui.page0 = ui.zone_page[0];
          ui.page1 = ui.zone_page[2];
          ui.focus = g_state.focus_zone;
          ui.display_topology = static_cast<uint8_t>(g_state.display_topology);
          ui.flip0 = g_state.screen_cfg[0].flip_180;
          ui.flip1 = g_state.screen_cfg[1].flip_180;
          ui.page_units_mask = g_state.page_units_mask;
          ui.page_alert_max_mask = g_state.page_alert_max_mask;
          ui.page_alert_min_mask = g_state.page_alert_min_mask;
          const uint32_t t0 = millis();
          g_nvs.saveUiPersist(ui);
          const uint32_t dt = millis() - t0;
          ++g_state.persist_audit.ui_writes;
          if (dt > g_state.persist_audit.ui_write_max_ms) {
            g_state.persist_audit.ui_write_max_ms = dt;
          }
          if (g_state.oled_primary_ready) {
            g_oled_primary.drawLines("REBOOT REQUIRED", "DISPLAY MODE", nullptr,
                                     nullptr);
          }
          LOGI("Display mode change -> rebooting\r\n");
          g_twai.stop();
          g_twai.uninstall();
          AppSleepMs(300);
          ESP.restart();
        }
        if (save_screens) {
          auto zone_active = [&](uint8_t z) -> bool {
            switch (g_state.display_topology) {
              case DisplayTopology::kSmallOnly:
                return z == 0;
              case DisplayTopology::kDualSmall:
                if (z == 0) return true;
                if (z == 2) return g_state.dual_screens;
                return false;
              case DisplayTopology::kLargeOnly:
                return z == 0 || z == 1;
              case DisplayTopology::kLargePlusSmall:
                if (z == 0 || z == 1) return true;
                if (z == 2) return g_state.dual_screens;
                return false;
              case DisplayTopology::kUnconfigured:
              default:
                return z == 0;
            }
          };
          WithStateLock([&]() {
            for (uint8_t z = 0; z < kMaxZones; ++z) {
              if (!zone_active(z)) continue;
              g_state.boot_page_index[z] = g_state.page_index[z];
            }
          });
          BuildUiPersistFromState(g_state);
          PersistRequestUiSave();
          markUiDirty(now_ms);
        }
        if (topo != prev_topo && !display_mode_changed) {
          WithStateLock([&]() {
            g_state.display_topology = topo;
          });
          markUiDirty(now_ms);
        }
        const bool new_oil_swap =
            (g_state.user_sensor[0].source == UserSensorSource::kSensor2) &&
            (g_state.user_sensor[1].source == UserSensorSource::kSensor1);
        if (new_oil_swap != prev_oil_swap) {
          g_state.oil_cfg.swap = new_oil_swap;
          strlcpy(g_state.oil_cfg.pressure_label, g_state.user_sensor[0].label,
                  sizeof(g_state.oil_cfg.pressure_label));
          strlcpy(g_state.oil_cfg.temp_label, g_state.user_sensor[1].label,
                  sizeof(g_state.oil_cfg.temp_label));
          OilPersist op{};
          op.mode = static_cast<uint8_t>(g_state.oil_cfg.mode);
          op.swap = g_state.oil_cfg.swap;
          strlcpy(op.label_p, g_state.oil_cfg.pressure_label, sizeof(op.label_p));
          strlcpy(op.label_t, g_state.oil_cfg.temp_label, sizeof(op.label_t));
          PersistRequestOilSave(op);
          forceAll();
        }
        if (reset_baro_request) {
          g_state.baro_reset_request = true;
        }
        if (toggle_hide_page) {
          const uint8_t page = g_state.page_index[focus] % kPageCount;
          const uint32_t page_mask = PageMaskAll(kPageCount);
          const uint32_t bit = static_cast<uint32_t>(1U << page);
          bool applied = false;
          WithStateLock([&]() {
            const uint32_t new_mask = g_state.page_hidden_mask ^ bit;
            if (new_mask != page_mask) {  // prevent hiding all pages
              g_state.page_hidden_mask = new_mask;
              EnsureVisiblePages(g_state);
              if (IsPageHidden(g_state, page)) {
                g_state.page_index[focus] = NextVisiblePage(g_state, page, +1);
              }
              applied = true;
            }
          });
          if (applied) {
            UiPersist ui = BuildUiPersistFromState(g_state);
            g_nvs.saveUiPersist(ui);
            forceAll();
          }
        }
        if (unhide_all_pages) {
          WithStateLock([&]() {
            g_state.page_hidden_mask = 0;
            EnsureVisiblePages(g_state);
          });
          UiPersist ui = BuildUiPersistFromState(g_state);
          g_nvs.saveUiPersist(ui);
          forceAll();
        }
        if (g_state.reset_all_max_request) {
          resetAllMax(g_state, now_ms);
          g_state.reset_all_max_request = false;
          forceAll();
          renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                   now_ms, true, true, g_alerts);
        }
#if SETUP_WIZARD_ENABLED
        if (start_setup) {
          g_state.ui_menu.exit();
          g_setup_wizard.begin(focus, g_state);
          forceAll();
          renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                   now_ms, true, true, g_alerts);
          continue;
        }
#endif
        if (start_wifi_mode) {
          g_state.ui_menu.exit();
          WithStateLock([&]() {
            g_state.wifi_mode_active = true;
            g_state.wifi_exit_confirm = false;
          });
          WifiPortalEnter();
          g_oled_secondary.simpleClear();
          g_oled_secondary.simpleSend();
          g_oled_secondary.setSleep(true);
          forceAll();
          renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                   now_ms, true, true, g_alerts);
          continue;
        }
        if (factory_reset) {
          g_state.ui_menu.exit();
          forceAll();
          renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                   now_ms, true, true, g_alerts);
          LOGW("FACTORY RESET: wiping NVS and rebooting...\r\n");
          g_twai.stop();
          g_twai.uninstall();
          g_nvs.factoryResetClearAll();
          AppSleepMs(200);
          ESP.restart();
        }
        if (start_edit_max || start_edit_min) {
          const EditModeState::Mode mode =
              start_edit_max ? EditModeState::Mode::kEditMax
                             : EditModeState::Mode::kEditMin;
          g_state.ui_menu.exit();
          g_state.edit_mode.mode[focus] = mode;
          g_state.edit_mode.page[focus] = cur_page;
          // Initialize edit value: if threshold is NAN, seed from live data; else use existing.
          float canon_seed = start_edit_max ? g_state.thresholds[cur_page].max
                                            : g_state.thresholds[cur_page].min;
          if (isnan(canon_seed)) {
            float live_val = NAN;
            if (PageCanonicalValue(currentPageId(g_state, focus), g_state,
                                   DisplayConfigForZone(g_state, focus), ActiveStore(),
                                   now_ms, live_val) &&
                !isnan(live_val)) {
              canon_seed = live_val;
            } else {
              canon_seed = 0.0f;
            }
          }
          g_state.edit_mode.display_value[focus] =
              CanonToDisplay(page_kind, canon_seed,
                             DisplayConfigForZone(g_state, focus));
          g_state.edit_mode.last_activity_ms[focus] = now_ms;
          g_state.edit_mode.locked_maxmin[focus] = true;
          g_state.edit_mode.locked_is_max[focus] = (mode == EditModeState::Mode::kEditMax);
          g_state.force_redraw[focus] = true;
          renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                   now_ms, true, true, g_alerts);
          continue;
        }
        if (DisplayConfigForZone(g_state, focus).flip_180 != prev_flip) {
          const PhysicalDisplayId disp = ZoneToDisplay(g_state, focus);
          if (disp == PhysicalDisplayId::kPrimary && g_state.oled_primary_ready) {
            g_oled_primary.setRotation(DisplayConfigForZone(g_state, focus).flip_180);
          } else if (disp == PhysicalDisplayId::kSecondary &&
                     g_state.oled_secondary_ready) {
            g_oled_secondary.setRotation(DisplayConfigForZone(g_state, focus).flip_180);
          }
          g_state.force_redraw[focus] = true;
          markUiDirty(now_ms);
        }
        if (page_units != prev_page_units) {
          WithStateLock([&]() {
            SetPageUnits(g_state, cur_page, page_units);
          });
          for (uint8_t scr = 0; scr < zone_count; ++scr) {
            if ((g_state.page_index[scr] % kPageCount) == cur_page) {
              g_state.force_redraw[scr] = true;
            }
          }
          markUiDirty(now_ms);
        }
        if (max_alert_enabled != prev_max_alert_enabled) {
          WithStateLock([&]() {
            SetPageMaxAlertEnabled(g_state, cur_page, max_alert_enabled);
          });
          forceAll();
          markUiDirty(now_ms);
        }
        if (min_alert_enabled != prev_min_alert_enabled) {
          WithStateLock([&]() {
            SetPageMinAlertEnabled(g_state, cur_page, min_alert_enabled);
          });
          forceAll();
          markUiDirty(now_ms);
        }
        const bool flip0_changed = g_state.screen_cfg[0].flip_180 != prev_flip0;
        const bool flip1_changed = g_state.screen_cfg[1].flip_180 != prev_flip1;
        if (flip0_changed) {
          if (g_state.oled_primary_ready) {
            g_oled_primary.setRotation(g_state.screen_cfg[0].flip_180);
          }
          for (uint8_t z = 0; z < zone_count && z < kMaxZones; ++z) {
            if (ZoneToDisplay(g_state, z) == PhysicalDisplayId::kPrimary) {
              g_state.force_redraw[z] = true;
            }
          }
          markUiDirty(now_ms);
        }
        if (flip1_changed) {
          if (g_state.oled_secondary_ready) {
            g_oled_secondary.setRotation(g_state.screen_cfg[1].flip_180);
          }
          for (uint8_t z = 0; z < zone_count && z < kMaxZones; ++z) {
            if (ZoneToDisplay(g_state, z) == PhysicalDisplayId::kSecondary) {
              g_state.force_redraw[z] = true;
            }
          }
          markUiDirty(now_ms);
        }
        forceAll();
        // Menu navigation does not rely on long-hold rendering hints.
        renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary, now_ms,
                 true, true, g_alerts);
      }
      continue;
    }

    // Seven rapid short-clicks (≤2.5s window) enter Wi-Fi mode.
    static uint8_t wifi_clicks = 0;
    static uint32_t wifi_window_start_ms = 0;
    constexpr uint32_t kWifiClickWindowMs = 2500;
    if (action == UiAction::kClick1) {
      const bool window_expired =
          (wifi_clicks == 0) ||
          (now_ms - wifi_window_start_ms > kWifiClickWindowMs);
      if (window_expired) {
        wifi_clicks = 0;
        wifi_window_start_ms = now_ms;
      }
      ++wifi_clicks;
      if (wifi_clicks >= 7 && !g_state.wifi_mode_active) {
        wifi_clicks = 0;
        WithStateLock([&]() {
          g_state.wifi_mode_active = true;
          g_state.wifi_exit_confirm = false;
        });
        WifiPortalEnter();
        g_oled_secondary.simpleClear();
        g_oled_secondary.simpleSend();
        g_oled_secondary.setSleep(true);
        forceAll();
        renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                 now_ms, true, true, g_alerts);
        continue;
      }
    } else {
      // Any other action cancels the 7-click sequence.
      wifi_clicks = 0;
    }

    switch (action) {
      case UiAction::kClick1: {
        WithStateLock([&]() {
          EnsureVisiblePages(g_state);
          g_state.page_index[focus] =
              NextVisiblePage(g_state, g_state.page_index[focus], +1);
        });
        g_state.force_redraw[focus] = true;
        break;
      }
      case UiAction::kClick7: {
        if (!g_state.wifi_mode_active) {
          enterWifiMode();
          continue;
        }
        break;
      }
      case UiAction::kLong:
        WithStateLock([&]() {
          EnsureVisiblePages(g_state);
          g_state.page_index[focus] =
              NextVisiblePage(g_state, g_state.page_index[focus], -1);
        });
        g_state.force_redraw[focus] = true;
        g_state.allow_oled2_during_hold = true;
        forceAll();
        break;
      case UiAction::kClick1Long:
        resetMaxForFocusPage(g_state, now_ms);
        g_state.force_redraw[focus] = true;
        g_state.allow_oled2_during_hold = true;
        forceAll();
        break;
      case UiAction::kClick2:
        if (GetActiveZoneCount(g_state) > 1) {
          WithStateLock([&]() {
            g_state.focus_zone = NextZone(g_state, focus);
          });
          forceAll();
          renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                   now_ms, true, true, g_alerts);
        }
        break;
      case UiAction::kClick3:
        g_state.ui_menu.enter(
            ZoneToDisplay(g_state, focus) == PhysicalDisplayId::kPrimary ? 0 : 1);
        forceAll();
        renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                 now_ms, true, true, g_alerts);
        break;
      case UiAction::kClick5:
        {
          bool sleep_now = false;
          WithStateLock([&]() {
            g_state.sleep = !g_state.sleep;
            sleep_now = g_state.sleep;
          });
          if (!sleep_now) {
            if (g_state.oled_primary_ready) g_oled_primary.setSleep(false);
            if (g_state.oled_secondary_ready) g_oled_secondary.setSleep(false);
          } else {
            if (g_state.oled_primary_ready) g_oled_primary.setSleep(true);
            if (g_state.oled_secondary_ready) g_oled_secondary.setSleep(true);
          }
        }
        forceAll();
        renderUi(g_state, ActiveStore(), g_oled_primary, g_oled_secondary,
                 now_ms, true, true, g_alerts);
        break;
      case UiAction::kClick4:
        g_state.extrema_view.active[focus] = true;
        g_state.extrema_view.show_min[focus] = false;
        g_state.extrema_view.page[focus] = currentPageIndex(g_state, focus);
        g_state.force_redraw[focus] = true;
        break;
      case UiAction::kNone:
      default:
        break;
    }
  }
}

