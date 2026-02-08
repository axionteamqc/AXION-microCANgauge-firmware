#include "settings/nvs_store.h"

#include <Preferences.h>

bool NvsStore::loadUiPersist(UiPersist& out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  out.page0 = prefs.getUChar(kKeyUiPage0, 0);
  out.page1 = prefs.getUChar(kKeyUiPage1, 0);
  out.zone_page[0] = prefs.getUChar(kKeyUiZone0, out.page0);
  out.zone_page[1] = prefs.getUChar(kKeyUiZone1, out.page0);
  out.zone_page[2] = prefs.getUChar(kKeyUiZone2, out.page1);
  out.boot_page[0] = prefs.getUChar(kKeyUiBoot0, out.zone_page[0]);
  out.boot_page[1] = prefs.getUChar(kKeyUiBoot1, out.zone_page[1]);
  out.boot_page[2] = prefs.getUChar(kKeyUiBoot2, out.zone_page[2]);
  out.focus = prefs.getUChar(kKeyUiFocus, 0);
  out.display_topology = prefs.getUChar(
      kKeyUiDisplayTopo, static_cast<uint8_t>(DisplayTopology::kSmallOnly));
  out.has_display_topology = prefs.isKey(kKeyUiDisplayTopo);
  out.units0 = prefs.getBool(kKeyUiUnits0, false);
  out.units1 = prefs.getBool(kKeyUiUnits1, false);
  out.flip0 = prefs.getBool(kKeyUiFlip0, false);
  out.flip1 = prefs.getBool(kKeyUiFlip1, false);
  if (prefs.isKey(kKeyUiPageUnits32)) {
    out.page_units_mask = prefs.getUInt(kKeyUiPageUnits32, 0);
  } else {
    out.page_units_mask = static_cast<uint32_t>(prefs.getUShort(kKeyUiPageUnits, 0));
  }
  if (prefs.isKey("ui_am32") || prefs.isKey("ui_al32")) {
    out.page_alert_max_mask = prefs.getUInt("ui_am32", 0);
    out.page_alert_min_mask = prefs.getUInt("ui_al32", 0);
  } else {
    out.page_alert_max_mask = static_cast<uint32_t>(prefs.getUShort("ui_am", 0));
    out.page_alert_min_mask = static_cast<uint32_t>(prefs.getUShort("ui_al", 0));
  }
  if (prefs.isKey(kKeyUiHidden32)) {
    out.page_hidden_mask = prefs.getUInt(kKeyUiHidden32, 0);
  } else {
    out.page_hidden_mask = static_cast<uint32_t>(prefs.getUShort(kKeyUiHidden, 0));
  }
  out.demo_mode = prefs.getBool(kKeyUiDemo, false);
  String ecu = prefs.getString(kKeyUiEcu, "MS3");
  ecu.toCharArray(out.ecu_type, sizeof(out.ecu_type));
  out.has_page_units = prefs.isKey(kKeyUiPageUnits32) || prefs.isKey(kKeyUiPageUnits);
  out.has_alert_masks = (prefs.isKey("ui_am32") || prefs.isKey("ui_al32") ||
                         (prefs.isKey("ui_am") && prefs.isKey("ui_al")));
  out.has_page_hidden = prefs.isKey(kKeyUiHidden32) || prefs.isKey(kKeyUiHidden);
  out.has_page0 = prefs.isKey(kKeyUiPage0);
  out.has_page1 = prefs.isKey(kKeyUiPage1);
  out.has_focus = prefs.isKey(kKeyUiFocus);
  out.has_display_topology = prefs.isKey(kKeyUiDisplayTopo);
  out.has_zone_pages =
      prefs.isKey(kKeyUiZone0) && prefs.isKey(kKeyUiZone1) && prefs.isKey(kKeyUiZone2);
  out.has_boot_pages =
      prefs.isKey(kKeyUiBoot0) || prefs.isKey(kKeyUiBoot1) || prefs.isKey(kKeyUiBoot2);
  if (!out.has_zone_pages) {
    // Migration: map legacy screen0->zone0, screen1->zone2, set zone1=zone0.
    out.zone_page[0] = out.page0;
    out.zone_page[1] = out.page0;
    out.zone_page[2] = out.page1;
  }
  if (!out.has_boot_pages) {
    out.boot_page[0] = out.zone_page[0];
    out.boot_page[1] = out.zone_page[1];
    out.boot_page[2] = out.zone_page[2];
  }
  prefs.end();
  return true;
}

bool NvsStore::saveUiPersist(const UiPersist& in) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  // Zone/page persistence
  bool ok = true;
  ok &= PutBoolChecked(prefs, kKeyUiHasZones, true);
  ok &= PutUCharChecked(prefs, kKeyUiZone0, in.zone_page[0]);
  ok &= PutUCharChecked(prefs, kKeyUiZone1, in.zone_page[1]);
  ok &= PutUCharChecked(prefs, kKeyUiZone2, in.zone_page[2]);
  // Legacy screen keys (derive from zones)
  const bool has_secondary =
      (in.display_topology == static_cast<uint8_t>(DisplayTopology::kDualSmall)) ||
      (in.display_topology == static_cast<uint8_t>(DisplayTopology::kLargePlusSmall));
  const uint8_t legacy_page1 = has_secondary ? in.zone_page[2] : in.zone_page[1];
  ok &= PutUCharChecked(prefs, kKeyUiPage0, in.zone_page[0]);
  ok &= PutUCharChecked(prefs, kKeyUiPage1, legacy_page1);
  ok &= PutUCharChecked(prefs, kKeyUiBoot0, in.boot_page[0]);
  ok &= PutUCharChecked(prefs, kKeyUiBoot1, in.boot_page[1]);
  ok &= PutUCharChecked(prefs, kKeyUiBoot2, in.boot_page[2]);
  ok &= PutUCharChecked(prefs, kKeyUiFocus, in.focus);
  ok &= PutUCharChecked(prefs, kKeyUiDisplayTopo, in.display_topology);
  // Deprecated: units0/1 (screen-global). Keep keys but write false.
  ok &= PutBoolChecked(prefs, kKeyUiUnits0, false);
  ok &= PutBoolChecked(prefs, kKeyUiUnits1, false);
  ok &= PutBoolChecked(prefs, kKeyUiFlip0, in.flip0);
  ok &= PutBoolChecked(prefs, kKeyUiFlip1, in.flip1);
  ok &= PutUIntChecked(prefs, "ui_pu32", in.page_units_mask);
  ok &= PutUIntChecked(prefs, "ui_am32", in.page_alert_max_mask);
  ok &= PutUIntChecked(prefs, "ui_al32", in.page_alert_min_mask);
  ok &= PutUIntChecked(prefs, "ui_hid32", in.page_hidden_mask);
  ok &= PutUShortChecked(prefs, kKeyUiPageUnits,
                         static_cast<uint16_t>(in.page_units_mask & 0xFFFFu));
  ok &= PutUShortChecked(prefs, "ui_am",
                         static_cast<uint16_t>(in.page_alert_max_mask & 0xFFFFu));
  ok &= PutUShortChecked(prefs, "ui_al",
                         static_cast<uint16_t>(in.page_alert_min_mask & 0xFFFFu));
  ok &= PutUShortChecked(prefs, kKeyUiHidden,
                         static_cast<uint16_t>(in.page_hidden_mask & 0xFFFFu));
  ok &= PutBoolChecked(prefs, kKeyUiDemo, in.demo_mode);
  ok &= PutStringChecked(prefs, kKeyUiEcu, in.ecu_type);
  prefs.end();
  return ok;
}
