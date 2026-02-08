#include "ui_menu.h"

#include <cstring>

#include <Arduino.h>

#include "app_state.h"
#include "app/app_globals.h"
#include "app/app_ui_snapshot.h"
#include "app/app_runtime.h"
#include "app/can_state_snapshot.h"
#include "app/can_runtime.h"
#include "config/factory_config.h"
#include "can_rx.h"
#include "settings/ui_persist_build.h"
#include "ui/pages.h"
#include "ui/ui_menu_internal.h"

template <typename F>
static inline void WithStateLock(F&& fn) {
  portENTER_CRITICAL(&g_state_mux);
  fn();
  portEXIT_CRITICAL(&g_state_mux);
}

void UiMenu::handleAction(UiAction action, ScreenSettings& cfg, bool& reset_all_max,
                          bool& start_quick_setup, ValueKind page_kind,
                          bool& page_units_imperial,
                          bool& factory_reset, bool& max_alert_enabled,
                          bool& min_alert_enabled, DisplayTopology& display_topology,
                          bool& save_screens, bool& display_mode_changed,
                          bool& rerun_display_setup, bool& start_edit_max,
                          bool& start_edit_min, bool& start_wifi_mode,
                          bool& toggle_hide_page, bool& unhide_all_pages,
                          bool& reset_baro_request) {
  if (!active_) {
    return;
  }

  if (mode_ == MenuMode::kDisplayMode) {
    switch (action) {
      case UiAction::kClick1:
        display_item_ = nextDisplayModeItem();
        reset_confirm_ = false;
        break;
      case UiAction::kLong:
      case UiAction::kClick1Long:
        display_item_ = prevDisplayModeItem();
        reset_confirm_ = false;
        break;
      case UiAction::kClick2: {
        if (display_item_ == DisplayModeItem::kBack) {
          mode_ = MenuMode::kDeviceSetup;
          device_item_ = DeviceSetupItem::kDisplay;
        } else {
          DisplayTopology new_topo = topoForItem(display_item_);
          if (new_topo != display_topology) {
            display_topology = new_topo;
            display_mode_changed = true;
          }
          mode_ = MenuMode::kDeviceSetup;
          device_item_ = DeviceSetupItem::kDisplay;
        }
        break;
      }
      case UiAction::kClick3:
        exit();
        break;
      default:
        break;
    }
    return;
  }

  if (mode_ == MenuMode::kCanDiagnostics) {
    bool request_exit = false;
    HandleCanDiagnosticsAction(action, can_diag_page_, can_diag_test_active_,
                               can_diag_test_start_ms_, request_exit, mode_,
                               device_item_);
    if (request_exit) {
      exit();
    }
    return;
  }
  if (mode_ == MenuMode::kPageSetup) {
    const bool units_available =
        (page_kind == ValueKind::kPressure) || (page_kind == ValueKind::kBoost) ||
        (page_kind == ValueKind::kTemp) || (page_kind == ValueKind::kSpeed);
    switch (action) {
      case UiAction::kClick1:
        page_item_ = nextPageSetupItem(units_available);
        reset_confirm_ = false;
        break;
      case UiAction::kLong:
      case UiAction::kClick1Long:
        page_item_ = prevPageSetupItem(units_available);
        reset_confirm_ = false;
        break;
      case UiAction::kClick2:
        switch (page_item_) {
          case PageSetupItem::kUnits:
            if (units_available) {
              page_units_imperial = !page_units_imperial;
            }
            break;
          case PageSetupItem::kMaxAlert:
            max_alert_enabled = !max_alert_enabled;
            break;
          case PageSetupItem::kMinAlert:
            min_alert_enabled = !min_alert_enabled;
            break;
          case PageSetupItem::kMaxLimit:
            start_edit_max = true;
            break;
          case PageSetupItem::kMinLimit:
            start_edit_min = true;
            break;
          case PageSetupItem::kHidePage:
            toggle_hide_page = true;
            break;
          case PageSetupItem::kBack:
            mode_ = MenuMode::kRoot;
            item_ = Item::kPageSetup;
            reset_confirm_ = false;
            break;
          case PageSetupItem::kCount:
          default:
            break;
        }
        break;
      case UiAction::kClick3:
        exit();
        break;
      default:
        break;
    }
    return;
  }

  if (mode_ == MenuMode::kAbout) {
    switch (action) {
      case UiAction::kClick1:
        about_item_ = nextAboutItem();
        break;
      case UiAction::kLong:
      case UiAction::kClick1Long:
        about_item_ = prevAboutItem();
        break;
      case UiAction::kClick2:
        mode_ = MenuMode::kRoot;
        item_ = Item::kAbout;
        break;
      case UiAction::kClick3:
        exit();
        break;
      default:
        break;
    }
    return;
  }

  if (mode_ == MenuMode::kDeviceSetup) {
    switch (action) {
      case UiAction::kClick1:
        device_item_ = static_cast<DeviceSetupItem>(
            (static_cast<uint8_t>(device_item_) + 1) %
            static_cast<uint8_t>(DeviceSetupItem::kCount));
        reset_confirm_ = false;
        break;
      case UiAction::kLong:
      case UiAction::kClick1Long: {
        const uint8_t count = static_cast<uint8_t>(DeviceSetupItem::kCount);
        const uint8_t cur = static_cast<uint8_t>(device_item_);
        device_item_ = static_cast<DeviceSetupItem>((cur + count - 1) % count);
        reset_confirm_ = false;
        break;
      }
      case UiAction::kClick2:
        switch (device_item_) {
          case DeviceSetupItem::kDisplay:
            mode_ = MenuMode::kDisplayMode;
            display_item_ = itemForTopology(display_topology);
            break;
          case DeviceSetupItem::kCanSetup:
            {
              UiPersist ui{};
              WithStateLock([&]() {
                g_state.demo_mode = !g_state.demo_mode;
                ui = BuildUiPersistFromState(g_state);
                for (uint8_t z = 0; z < kMaxZones; ++z) {
                  g_state.force_redraw[z] = true;
                }
              });
              g_nvs.saveUiPersist(ui);
            }
            break;
          case DeviceSetupItem::kCanDiagnostics:
            mode_ = MenuMode::kCanDiagnostics;
            can_diag_page_ = 0;
            break;
          case DeviceSetupItem::kFlip0:
            WithStateLock([&]() {
              g_state.screen_cfg[0].flip_180 = !g_state.screen_cfg[0].flip_180;
            });
            break;
          case DeviceSetupItem::kFlip1:
            WithStateLock([&]() {
              g_state.screen_cfg[1].flip_180 = !g_state.screen_cfg[1].flip_180;
            });
            break;
          case DeviceSetupItem::kUnhideAllPages:
            unhide_all_pages = true;
            break;
          case DeviceSetupItem::kOilSensorSwap:
            {
              WithStateLock([&]() {
                const bool swapped =
                    (g_state.user_sensor[0].source == UserSensorSource::kSensor2) &&
                    (g_state.user_sensor[1].source == UserSensorSource::kSensor1);
                if (swapped) {
                  g_state.user_sensor[0].source = UserSensorSource::kSensor1;
                  g_state.user_sensor[1].source = UserSensorSource::kSensor2;
                } else {
                  g_state.user_sensor[0].source = UserSensorSource::kSensor2;
                  g_state.user_sensor[1].source = UserSensorSource::kSensor1;
                }
                g_state.oil_cfg.swap =
                    (g_state.user_sensor[0].source == UserSensorSource::kSensor2) &&
                    (g_state.user_sensor[1].source == UserSensorSource::kSensor1);
              });
            }
            break;
          case DeviceSetupItem::kRecalBaro:
            if (!reset_confirm_) {
              reset_confirm_ = true;
            } else {
              reset_baro_request = true;
              reset_confirm_ = false;
            }
            break;
          case DeviceSetupItem::kFactoryReset:
            if (!reset_confirm_) {
              reset_confirm_ = true;
            } else {
              factory_reset = true;
              reset_confirm_ = false;
            }
            break;
          case DeviceSetupItem::kBack:
            mode_ = MenuMode::kRoot;
            item_ = Item::kDeviceSetup;
            reset_confirm_ = false;
            break;
          case DeviceSetupItem::kCount:
          default:
            break;
        }
        break;
      case UiAction::kClick3:
        exit();
        break;
      default:
        break;
    }
    return;
  }

  switch (action) {
    case UiAction::kClick1:
      if (reset_confirm_) {
        reset_confirm_ = false;  // cancel confirm
      } else {
        item_ = nextItem();
        about_page_ = 0;
      }
      break;
    case UiAction::kLong:
    case UiAction::kClick1Long:
      if (reset_confirm_) {
        reset_confirm_ = false;  // cancel confirm
      } else {
        item_ = prevItem();
        about_page_ = 0;
      }
      break;
    case UiAction::kClick2:
      if (item_ == Item::kPageSetup) {
        mode_ = MenuMode::kPageSetup;
        const bool units_available =
            (page_kind == ValueKind::kPressure) || (page_kind == ValueKind::kBoost) ||
            (page_kind == ValueKind::kTemp) || (page_kind == ValueKind::kSpeed);
        page_item_ = units_available ? PageSetupItem::kUnits : PageSetupItem::kMaxAlert;
        reset_confirm_ = false;
      } else if (item_ == Item::kSaveScreens) {
        toast_until_ms_ = millis() + 700;
        save_screens = true;
      } else if (item_ == Item::kResetAllMax) {
        if (!reset_confirm_) {
          reset_confirm_ = true;
        } else {
          reset_all_max = true;
          reset_confirm_ = false;
          toast_until_ms_ = millis() + 1000;
        }
      } else if (item_ == Item::kDeviceSetup) {
        mode_ = MenuMode::kDeviceSetup;
        device_item_ = DeviceSetupItem::kDisplay;
        reset_confirm_ = false;
      } else if (item_ == Item::kWifiMode) {
        start_wifi_mode = true;
        exit();
      } else if (item_ == Item::kAbout) {
        mode_ = MenuMode::kAbout;
        about_item_ = AboutItem::kFw;
      }
      break;
    case UiAction::kClick3:
      exit();
      break;
    default:
      break;
  }
}

void UiMenu::render(OledU8g2& display, const ScreenSettings& cfg,
                    ValueKind page_kind, bool page_units_imperial,
                    bool max_alert_enabled, bool min_alert_enabled,
                    DisplayTopology display_topology, PhysicalDisplayId disp,
                    bool display_setup_confirm, const char* page_label,
                    uint8_t page_index, bool clear_buffer, uint8_t viewport_y,
                    uint8_t viewport_h, bool send_buffer) const {
  if (!active_) {
    return;
  }

  AppUiSnapshot ui;
  GetAppUiSnapshot(ui);

  if (clear_buffer) {
    display.simpleClear();
  }

  char header[16];
  snprintf(header, sizeof(header), "SETTINGS");
  char line2[24];
  line2[0] = '\0';

  auto topoShort = [](DisplayTopology topo) -> const char* {
    switch (topo) {
      case DisplayTopology::kSmallOnly:
        return "SONLY";
      case DisplayTopology::kDualSmall:
        return "2xS";
      case DisplayTopology::kLargeOnly:
        return "LONLY";
      case DisplayTopology::kLargePlusSmall:
        return "L+S";
      case DisplayTopology::kUnconfigured:
      default:
        return "UNCFG";
    }
  };

  if (mode_ == MenuMode::kCanDiagnostics) {
    RenderCanDiagnostics(display, viewport_y, viewport_h, send_buffer,
                         can_diag_page_, can_diag_test_active_,
                         can_diag_test_start_ms_);
    return;
  }

  if (mode_ == MenuMode::kPageSetup) {
    const bool units_available =
        (page_kind == ValueKind::kPressure) || (page_kind == ValueKind::kBoost) ||
        (page_kind == ValueKind::kTemp) || (page_kind == ValueKind::kSpeed);
    PageSetupItem render_item = page_item_;
    if (!units_available && render_item == PageSetupItem::kUnits) {
      // Skip rendering units when not applicable.
      render_item = PageSetupItem::kMaxAlert;
    }
    snprintf(header, sizeof(header), "PAGE SETUP");
    const char* label = (page_label && page_label[0]) ? page_label : "PAGE";
    const uint8_t safe_page =
        (page_index < kPageCount) ? page_index : static_cast<uint8_t>(0);
    const float thr_min = ui.thresholds[safe_page].min;
    const float thr_max = ui.thresholds[safe_page].max;
    switch (render_item) {
      case PageSetupItem::kUnits:
        switch (page_kind) {
          case ValueKind::kPressure:
          case ValueKind::kBoost:
            snprintf(line2, sizeof(line2), "%s Units: %s", label,
                     page_units_imperial ? "PSI" : "kPa");
            break;
          case ValueKind::kTemp:
            snprintf(line2, sizeof(line2), "%s Units: %s", label,
                     page_units_imperial ? "F" : "C");
            break;
          case ValueKind::kSpeed:
            snprintf(line2, sizeof(line2), "%s Units: %s", label,
                     page_units_imperial ? "MPH" : "km/h");
            break;
          default:
            snprintf(line2, sizeof(line2), "%s Units: N/A", label);
            break;
        }
        break;
      case PageSetupItem::kMaxAlert:
        snprintf(line2, sizeof(line2), "%s Max: %s", label,
                 max_alert_enabled ? "On" : "Off");
        break;
      case PageSetupItem::kMinAlert:
        snprintf(line2, sizeof(line2), "%s Min: %s", label,
                 min_alert_enabled ? "On" : "Off");
        break;
      case PageSetupItem::kMaxLimit: {
        const bool has_val = !isnan(thr_max);
        char buf[12];
        if (!has_val) {
          strlcpy(buf, "NA", sizeof(buf));
        } else {
          const float disp = CanonToDisplay(page_kind, thr_max, cfg);
          switch (page_kind) {
            case ValueKind::kVoltage:
            case ValueKind::kAfr:
            case ValueKind::kPercent:
            case ValueKind::kPressure:
            case ValueKind::kBoost:
              snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(disp));
              break;
            case ValueKind::kTemp:
            case ValueKind::kSpeed:
            case ValueKind::kDeg:
            case ValueKind::kRpm:
              snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(disp));
              break;
            default:
              snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(disp));
              break;
          }
        }
        snprintf(line2, sizeof(line2), "%s Max: %s", label, buf);
        break;
      }
      case PageSetupItem::kMinLimit: {
        const bool has_val = !isnan(thr_min);
        char buf[12];
        if (!has_val) {
          strlcpy(buf, "NA", sizeof(buf));
        } else {
          const float disp = CanonToDisplay(page_kind, thr_min, cfg);
          switch (page_kind) {
            case ValueKind::kVoltage:
            case ValueKind::kAfr:
            case ValueKind::kPercent:
            case ValueKind::kPressure:
            case ValueKind::kBoost:
              snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(disp));
              break;
            case ValueKind::kTemp:
            case ValueKind::kSpeed:
            case ValueKind::kDeg:
            case ValueKind::kRpm:
              snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(disp));
              break;
            default:
              snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(disp));
              break;
          }
        }
        snprintf(line2, sizeof(line2), "%s Min: %s", label, buf);
        break;
      }
      case PageSetupItem::kHidePage: {
        const bool hidden = (ui.page_hidden_mask & (1U << safe_page)) != 0;
        snprintf(line2, sizeof(line2), "%s Hide: %s", label,
                 hidden ? "On" : "Off");
        break;
      }
      case PageSetupItem::kBack:
        snprintf(line2, sizeof(line2), "Back");
        break;
      case PageSetupItem::kCount:
      default:
        break;
    }
  } else {
    switch (mode_) {
      case MenuMode::kDisplayMode: {
        snprintf(header, sizeof(header), "DISPLAY MODE");
        const char* label = "1xSmall";
        switch (display_item_) {
          case DisplayModeItem::kSmallOnly:
            label = "1xSmall";
            break;
          case DisplayModeItem::kDualSmall:
            label = "2xSmall";
            break;
          case DisplayModeItem::kLargeOnly:
            label = "1xLarge";
            break;
          case DisplayModeItem::kLargePlusSmall:
            label = "S+L";
            break;
          case DisplayModeItem::kBack:
            label = "BACK";
            break;
          case DisplayModeItem::kCount:
          default:
            break;
        }
        snprintf(line2, sizeof(line2), "%s", label);
        break;
      }
      case MenuMode::kDeviceSetup: {
        snprintf(header, sizeof(header),
                 (device_item_ == DeviceSetupItem::kCanSetup) ? "CAN SETUP"
                                                              : "DEVICE SETUP");
        switch (device_item_) {
          case DeviceSetupItem::kDisplay:
            snprintf(line2, sizeof(line2), "Display: %s",
                     topoShort(display_topology));
            break;
          case DeviceSetupItem::kCanSetup:
            snprintf(line2, sizeof(line2), "Mode: %s",
                     ui.demo_mode ? "DEMO" : "CAN");
            break;
          case DeviceSetupItem::kCanDiagnostics:
            snprintf(line2, sizeof(line2), "CAN DIAGNOSTICS");
            break;
          case DeviceSetupItem::kFlip0:
            snprintf(line2, sizeof(line2), "Flip OLED1: %s",
                     ui.screen_flip[0] ? "On" : "Off");
            break;
          case DeviceSetupItem::kFlip1:
            snprintf(line2, sizeof(line2), "Flip OLED2: %s",
                     ui.screen_flip[1] ? "On" : "Off");
            break;
          case DeviceSetupItem::kUnhideAllPages:
            snprintf(line2, sizeof(line2), "UNHIDE ALL PAGES");
            break;
          case DeviceSetupItem::kOilSensorSwap:
            {
              const bool swapped =
                  (ui.user_sensor[0].source == UserSensorSource::kSensor2) &&
                  (ui.user_sensor[1].source == UserSensorSource::kSensor1);
              snprintf(line2, sizeof(line2), "OIL SENSOR SWAP: %s",
                       swapped ? "ON" : "OFF");
            }
            break;
          case DeviceSetupItem::kRecalBaro:
            snprintf(line2, sizeof(line2),
                     reset_confirm_ ? "Confirm BARO?" : "Recalibrate BARO");
            break;
          case DeviceSetupItem::kFactoryReset:
            snprintf(line2, sizeof(line2),
                     reset_confirm_ ? "Confirm reset?" : "Factory Reset");
            break;
          case DeviceSetupItem::kBack:
            snprintf(line2, sizeof(line2), "Back");
            break;
          case DeviceSetupItem::kCount:
          default:
            break;
        }
        break;
      }
      case MenuMode::kCanDiagnostics: {
        snprintf(header, sizeof(header), "CAN DIAGNOSTICS");
        snprintf(line2, sizeof(line2), "Press back");
        break;
      }
      case MenuMode::kAbout: {
        snprintf(header, sizeof(header), "ABOUT");
        switch (about_item_) {
          case AboutItem::kFw:
            snprintf(line2, sizeof(line2), "FW %s", kFirmwareVersion);
            break;
          case AboutItem::kBuild:
            snprintf(line2, sizeof(line2), "Build %s", kBuildId);
            break;
          case AboutItem::kMode:
            snprintf(line2, sizeof(line2), "Mode %s",
                     AppConfig::IsRealCanEnabled() ? "CAN" : "SIM");
            break;
          case AboutItem::kEcu: {
            const char* ecu = g_ecu_mgr.activeName();
            char buf[18];
            if (ecu && ecu[0] != '\0') {
              strlcpy(buf, ecu, sizeof(buf));
            } else {
              strlcpy(buf, "N/A", sizeof(buf));
            }
            snprintf(line2, sizeof(line2), "ECU: %s", buf);
            break;
          }
          case AboutItem::kDisplay: {
            const char* topo = topoShort(display_topology);
            snprintf(line2, sizeof(line2), "Display %s", topo);
            break;
          }
          case AboutItem::kCan: {
            char buf[18];
            const uint32_t rate = ui.can_bitrate_value;
            if (rate > 0) {
              const unsigned long kbps =
                  static_cast<unsigned long>((rate + 500UL) / 1000UL);
              snprintf(buf, sizeof(buf), "%lu kbps", kbps);
            } else {
              strlcpy(buf, "----", sizeof(buf));
            }
            snprintf(line2, sizeof(line2), "BR: %s", buf);
            break;
          }
          case AboutItem::kCanHealth: {
            const char* h = "OK";
            switch (ui.can_health) {
              case CanHealth::kNoFrames:
                h = "NO FRAMES";
                break;
              case CanHealth::kStale:
                h = "STALE";
                break;
              case CanHealth::kDecodeBad:
                h = "BAD CFG";
                break;
              case CanHealth::kImplausible:
                h = "DECODE ERR";
                break;
              case CanHealth::kOk:
              default:
                h = "OK";
                break;
            }
            if (ui.can_safe_listen) {
              snprintf(line2, sizeof(line2), "CAN: SAFE");
            } else {
              snprintf(line2, sizeof(line2), "CAN: %s", h);
            }
            break;
          }
          case AboutItem::kBaro: {
            if (ui.baro_acquired) {
              snprintf(line2, sizeof(line2), "BARO: %.1fkPa",
                       static_cast<double>(ui.baro_kpa));
            } else {
              snprintf(line2, sizeof(line2), "BARO: ----");
            }
            break;
          }
          case AboutItem::kBack:
            snprintf(line2, sizeof(line2), "Back");
            break;
          case AboutItem::kCount:
          default:
            break;
        }
        break;
      }
      case MenuMode::kRoot:
      default:
        const uint32_t now_ms = millis();
        if ((item_ == Item::kResetAllMax || item_ == Item::kSaveScreens) &&
            toast_until_ms_ > now_ms) {
          if (item_ == Item::kSaveScreens) {
            snprintf(header, sizeof(header), "SAVED");
            snprintf(line2, sizeof(line2), "BOOT PAGES");
          } else {
            snprintf(header, sizeof(header), "SETTINGS");
            snprintf(line2, sizeof(line2), "CLEARED");
          }
        } else {
          switch (item_) {
            case Item::kPageSetup:
              snprintf(line2, sizeof(line2), "%s SETUP",
                       (page_label && page_label[0]) ? page_label : "PAGE");
              break;
            case Item::kSaveScreens:
              snprintf(line2, sizeof(line2), "Save Screens");
              break;
            case Item::kResetAllMax:
              snprintf(line2, sizeof(line2),
                       reset_confirm_ ? "Confirm reset?" : "Reset Rmax/Rmin");
              break;
            case Item::kDeviceSetup:
              snprintf(line2, sizeof(line2), "Device setup");
              break;
            case Item::kWifiMode:
              snprintf(line2, sizeof(line2), "Wi-Fi mode");
              break;
            case Item::kAbout:
              snprintf(line2, sizeof(line2), "About");
              break;
            case Item::kCount:
            default:
              break;
          }
        }
        break;
    }
  }

  const uint8_t line_height = 12;
  uint8_t y = viewport_y + line_height;
  const uint8_t max_y =
      (viewport_h > 0) ? static_cast<uint8_t>(viewport_y + viewport_h) : 255;
  display.simpleSetFontSmall();
  if (header[0] && y < max_y) {
    display.simpleDrawStr(0, y, header);
    y = static_cast<uint8_t>(y + line_height);
  }
  if (line2[0] && y < max_y) {
    display.simpleDrawStr(0, y, line2);
  }
  if (send_buffer) {
    display.simpleSend();
  }
}
