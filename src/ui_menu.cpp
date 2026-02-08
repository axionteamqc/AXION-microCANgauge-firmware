#include "ui_menu.h"

#include <cstring>

#include <Arduino.h>

#include "app_state.h"
#include "app/app_globals.h"
#include "app/app_runtime.h"
#include "app/can_state_snapshot.h"
#include "app/can_runtime.h"
#include "config/factory_config.h"
#include "can_rx.h"
#include "settings/ui_persist_build.h"
#include "ui/pages.h"


UiMenu::UiMenu()
    : active_(false),
      item_(Item::kPageSetup),
      mode_(MenuMode::kRoot),
      page_item_(PageSetupItem::kUnits),
      device_item_(DeviceSetupItem::kDisplay),
      can_diag_page_(0),
      can_diag_test_active_(false),
      can_diag_test_start_ms_(0),
      display_item_(DisplayModeItem::kSmallOnly),
      about_item_(AboutItem::kFw),
      screen_index_(0),
      reset_confirm_(false),
      about_page_(0),
      toast_until_ms_(0) {}

bool UiMenu::isActive() const {
  return active_;
}

void UiMenu::enter(uint8_t screen_index) {
  active_ = true;
  screen_index_ = screen_index;
  item_ = Item::kPageSetup;
  mode_ = MenuMode::kRoot;
  page_item_ = PageSetupItem::kUnits;
  device_item_ = DeviceSetupItem::kDisplay;
  can_diag_page_ = 0;
  can_diag_test_active_ = false;
  can_diag_test_start_ms_ = 0;
  display_item_ = DisplayModeItem::kSmallOnly;
  about_item_ = AboutItem::kFw;
  reset_confirm_ = false;
}

void UiMenu::exit() {
  active_ = false;
  reset_confirm_ = false;
  mode_ = MenuMode::kRoot;
  page_item_ = PageSetupItem::kUnits;
  display_item_ = DisplayModeItem::kSmallOnly;
  about_item_ = AboutItem::kFw;
  can_diag_page_ = 0;
  can_diag_test_active_ = false;
  can_diag_test_start_ms_ = 0;
  toast_until_ms_ = 0;
}





UiMenu::Item UiMenu::currentItem() const {
  return item_;
}

uint8_t UiMenu::screenIndex() const {
  return screen_index_;
}

UiMenu::Item UiMenu::nextItem() const {
  return static_cast<Item>((static_cast<uint8_t>(item_) + 1) %
                           static_cast<uint8_t>(Item::kCount));
}

UiMenu::Item UiMenu::prevItem() const {
  const uint8_t count = static_cast<uint8_t>(Item::kCount);
  const uint8_t cur = static_cast<uint8_t>(item_);
  return static_cast<Item>((cur + count - 1) % count);
}

UiMenu::PageSetupItem UiMenu::nextPageSetupItem(bool units_available) const {
  PageSetupItem next = static_cast<PageSetupItem>(
      (static_cast<uint8_t>(page_item_) + 1) %
      static_cast<uint8_t>(PageSetupItem::kCount));
  if (!units_available && next == PageSetupItem::kUnits) {
    next = static_cast<PageSetupItem>(
        (static_cast<uint8_t>(next) + 1) %
        static_cast<uint8_t>(PageSetupItem::kCount));
  }
  return next;
}

UiMenu::PageSetupItem UiMenu::prevPageSetupItem(bool units_available) const {
  const uint8_t count = static_cast<uint8_t>(PageSetupItem::kCount);
  const uint8_t cur = static_cast<uint8_t>(page_item_);
  PageSetupItem prev =
      static_cast<PageSetupItem>((cur + count - 1) % count);
  if (!units_available && prev == PageSetupItem::kUnits) {
    prev = static_cast<PageSetupItem>(
        (static_cast<uint8_t>(prev) + count - 1) % count);
  }
  return prev;
}

UiMenu::DisplayModeItem UiMenu::nextDisplayModeItem() const {
  return static_cast<DisplayModeItem>(
      (static_cast<uint8_t>(display_item_) + 1) %
      static_cast<uint8_t>(DisplayModeItem::kCount));
}

UiMenu::DisplayModeItem UiMenu::prevDisplayModeItem() const {
  const uint8_t count = static_cast<uint8_t>(DisplayModeItem::kCount);
  const uint8_t cur = static_cast<uint8_t>(display_item_);
  return static_cast<DisplayModeItem>((cur + count - 1) % count);
}

UiMenu::DisplayModeItem UiMenu::itemForTopology(DisplayTopology topo) const {
  switch (topo) {
    case DisplayTopology::kSmallOnly:
      return DisplayModeItem::kSmallOnly;
    case DisplayTopology::kDualSmall:
      return DisplayModeItem::kDualSmall;
    case DisplayTopology::kLargeOnly:
      return DisplayModeItem::kLargeOnly;
    case DisplayTopology::kLargePlusSmall:
      return DisplayModeItem::kLargePlusSmall;
    case DisplayTopology::kUnconfigured:
    default:
      return DisplayModeItem::kSmallOnly;
  }
}

DisplayTopology UiMenu::topoForItem(DisplayModeItem item) const {
  switch (item) {
    case DisplayModeItem::kSmallOnly:
      return DisplayTopology::kSmallOnly;
    case DisplayModeItem::kDualSmall:
      return DisplayTopology::kDualSmall;
    case DisplayModeItem::kLargeOnly:
      return DisplayTopology::kLargeOnly;
    case DisplayModeItem::kLargePlusSmall:
      return DisplayTopology::kLargePlusSmall;
    default:
      return DisplayTopology::kSmallOnly;
  }
}

UiMenu::AboutItem UiMenu::nextAboutItem() const {
  return static_cast<AboutItem>(
      (static_cast<uint8_t>(about_item_) + 1) %
      static_cast<uint8_t>(AboutItem::kCount));
}

UiMenu::AboutItem UiMenu::prevAboutItem() const {
  const uint8_t count = static_cast<uint8_t>(AboutItem::kCount);
  const uint8_t cur = static_cast<uint8_t>(about_item_);
  return static_cast<AboutItem>((cur + count - 1) % count);
}
