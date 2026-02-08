#pragma once

#include <Arduino.h>

#include "drivers/oled_u8g2.h"

enum class ValueKind : uint8_t;

enum class PhysicalDisplayId : uint8_t;

enum class DisplayTopology : uint8_t {
  kUnconfigured = 0,
  kSmallOnly,
  kDualSmall,
  kLargeOnly,
  kLargePlusSmall,
};

enum class UiAction {
  kNone = 0,
  kClick1,
  kClick2,
  kClick3,
  kClick4,
  kClick5,
  kLong,
  kClick6,
  kClick7,
  kClick1Long,
  kLockGesture,
  kUnlockGesture
};

struct ScreenSettings {
  bool imperial_units;
  bool flip_180;
};

class UiMenu {
 public:
  enum class Item : uint8_t {
    kPageSetup = 0,
    kSaveScreens,
    kResetAllMax,
    kDeviceSetup,
    kWifiMode,
    kAbout,
    kCount
  };

  enum class MenuMode : uint8_t {
    kRoot = 0,
    kPageSetup,
    kDisplayMode,
    kAbout,
    kDeviceSetup,
    kCanDiagnostics
  };

  enum class PageSetupItem : uint8_t {
    kUnits = 0,
    kMaxAlert,
    kMinAlert,
    kMaxLimit,
    kMinLimit,
    kHidePage,
    kBack,
    kCount
  };

  enum class DeviceSetupItem : uint8_t {
    kDisplay = 0,
    kCanSetup,
    kCanDiagnostics,
    kFlip0,
    kFlip1,
    kUnhideAllPages,
    kOilSensorSwap,
    kRecalBaro,
    kFactoryReset,
    kBack,
    kCount
  };

  enum class DisplayModeItem : uint8_t {
    kSmallOnly = 0,
    kDualSmall,
    kLargeOnly,
    kLargePlusSmall,
    kBack,
    kCount
  };

  enum class AboutItem : uint8_t {
    kFw = 0,
    kBuild,
    kMode,
    kEcu,
    kDisplay,
    kCan,
    kCanHealth,
    kBaro,
    kBack,
    kCount
  };

  UiMenu();

  bool isActive() const;
  void enter(uint8_t screen_index);
  void exit();
  void handleAction(UiAction action, ScreenSettings& cfg, bool& reset_all_max,
                    bool& start_quick_setup, ValueKind page_kind,
                    bool& page_units_imperial,
                    bool& factory_reset, bool& max_alert_enabled,
                    bool& min_alert_enabled, DisplayTopology& display_topology,
                    bool& save_screens, bool& display_mode_changed,
                    bool& rerun_display_setup, bool& start_edit_max,
                    bool& start_edit_min, bool& start_wifi_mode,
                    bool& toggle_hide_page, bool& unhide_all_pages,
                    bool& reset_baro_request);
  void render(OledU8g2& display, const ScreenSettings& cfg,
              ValueKind page_kind, bool page_units_imperial,
              bool max_alert_enabled, bool min_alert_enabled,
              DisplayTopology display_topology, PhysicalDisplayId disp,
              bool display_setup_confirm, const char* page_label,
              uint8_t page_index, bool clear_buffer = true,
              uint8_t viewport_y = 0, uint8_t viewport_h = 0,
              bool send_buffer = true) const;
  Item currentItem() const;
  uint8_t screenIndex() const;

 private:
  Item nextItem() const;
  Item prevItem() const;
  PageSetupItem nextPageSetupItem(bool units_available) const;
  PageSetupItem prevPageSetupItem(bool units_available) const;
  DisplayModeItem nextDisplayModeItem() const;
  DisplayModeItem prevDisplayModeItem() const;
  DisplayModeItem itemForTopology(DisplayTopology topo) const;
  DisplayTopology topoForItem(DisplayModeItem item) const;
  AboutItem nextAboutItem() const;
  AboutItem prevAboutItem() const;

  bool active_;
  Item item_;
  MenuMode mode_;
  PageSetupItem page_item_;
  DeviceSetupItem device_item_;
  uint8_t can_diag_page_;
  bool can_diag_test_active_;
  uint32_t can_diag_test_start_ms_;
  DisplayModeItem display_item_;
  AboutItem about_item_;
  uint8_t screen_index_;
  bool reset_confirm_;
  uint8_t about_page_;
  mutable uint32_t toast_until_ms_;
};
