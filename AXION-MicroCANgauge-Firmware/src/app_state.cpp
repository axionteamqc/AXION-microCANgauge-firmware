#include "app_state.h"

#include <cstring>

#include "ui_render.h"
#include "app/page_mask.h"

void MockDataStore::update(uint32_t now_ms) {
  if (last_update_ms == 0) {
    last_update_ms = now_ms;
  }
  const uint32_t phase = now_ms / 100;
  rpm = 900 + (phase % 4200);                  // 900..5099
  speed_tenths = 250 + ((phase * 3) % 1200);   // 25.0..144.9
  coolant_c = 70 + ((phase / 2) % 35);         // 70..104
  batt_mv = 12300 + ((phase * 11) % 400);      // 12.3..12.7V
  rpm_valid = true;
  speed_valid = true;
  coolant_valid = true;
  batt_valid = true;
  last_update_ms = now_ms;
}

void initDefaults(AppState& s) {
  s.oled_primary_ready = false;
  s.oled_secondary_ready = false;
  s.dual_screens = false;
  s.focus_zone = 0;
  s.sleep = false;
  s.demo_mode = false;
  s.demo_tick_count = 0;
  s.can_ready = false;
  s.can_bitrate_locked = false;
  s.can_bitrate_value = 500000;
  s.last_can_rx_ms = 0;
  s.last_can_match_ms = 0;
  s.can_safe_listen = false;
  s.id_present_mask = 0;
  s.can_diag = AppState::CanDiag{};
  s.baro_acquired = false;
  s.baro_kpa = AppConfig::kDefaultBaroKpa;
  s.display_topology = DisplayTopology::kSmallOnly;
  s.display_setup_index = 0;
  s.can_stats = AppState::CanStats{};
  s.can_rates = AppState::CanRateWindow{};
  s.can_need_recover = false;
  s.can_recover_backoff_ms = 5000;
  s.can_recover_last_attempt_ms = 0;
  s.can_edge_prev = 0;
  s.can_edge_last_sample_ms = 0;
  s.can_edge_rate = 0.0f;
  s.can_edge_active = false;
  s.baro_reset_request = false;
  s.can_link = CanLinkDiag{};
  s.can_link.state = CanLinkState::kNoFrames;
  s.can_link.health = CanHealth::kNoFrames;
  s.can_link.last_health_change_ms = 0;
  s.can_link.last_change_ms = 0;
  s.can_link.window_start_ms = 0;
  s.can_link.rx_total_base = 0;
  s.can_link.rx_match_base = 0;
  s.can_link.rx_dash_base = 0;
  s.can_link.decode_oob_base = 0;

  for (uint8_t z = 0; z < kMaxZones; ++z) {
    s.page_index[z] = 0;
    s.max_blink_until_ms[z] = 0;
    s.max_blink_page[z] = 0;
    s.render_state[z] = RenderState{};
    s.screen_cfg[z] = {false, false};
  }

  s.reset_all_max_request = false;
  s.self_test_enabled = false;
  s.self_test = SelfTestState{};
  s.mock_data = MockDataStore{};
  s.ui_menu = UiMenu{};
  s.wizard_active = false;
  s.boot_ms = 0;
  s.ui_locked = false;
  s.oil_cfg = AppState::OilConfig{};
  s.oil_cfg.mode = AppState::OilConfig::Mode::kRaw;  // enable oil data by default
  s.user_sensor[0] = UserSensorCfg{};
  s.user_sensor[0].preset = UserSensorPreset::kOilPressure;
  s.user_sensor[0].kind = UserSensorKind::kPressure;
  s.user_sensor[0].source = UserSensorSource::kSensor1;
  strlcpy(s.user_sensor[0].label, "OILP", sizeof(s.user_sensor[0].label));
  s.user_sensor[0].scale = 1.0f;
  s.user_sensor[0].offset = 0.0f;
  s.user_sensor[1] = UserSensorCfg{};
  s.user_sensor[1].preset = UserSensorPreset::kOilTemp;
  s.user_sensor[1].kind = UserSensorKind::kTemp;
  s.user_sensor[1].source = UserSensorSource::kSensor2;
  strlcpy(s.user_sensor[1].label, "OILT", sizeof(s.user_sensor[1].label));
  s.user_sensor[1].scale = 1.0f;
  s.user_sensor[1].offset = 0.0f;
  s.stoich_afr = 14.7f;
  s.afr_show_lambda = false;
  for (size_t i = 0; i < kPageCount; ++i) {
    s.thresholds[i].min = NAN;
    s.thresholds[i].max = NAN;
  }
  s.edit_mode = {};
  s.extrema_view = {};
  for (size_t i = 0; i < static_cast<size_t>(SignalId::kCount); ++i) {
    s.last_good[i] = LastGoodSample{};
  }
  s.persist_audit = {};

  s.last_input_ms = 0;
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    s.force_redraw[z] = false;
    s.last_oled_ms[z] = 0;
  }
  s.btn_pressed = false;
  s.btn_pending_count = 0;
  s.allow_oled2_during_hold = false;
  s.page_units_mask = 0;
  const uint32_t all_disabled = 0;
  s.page_alert_max_mask = all_disabled;
  s.page_alert_min_mask = all_disabled;
  s.page_hidden_mask = 0;
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    s.boot_page_index[z] = s.page_index[z];
  }
  for (uint8_t p = 0; p < kPageCount; ++p) {
    s.page_recorded_max[p] = NAN;
    s.page_recorded_min[p] = NAN;
  }
}

static bool ZoneActive(const AppState& state, uint8_t z);

void resetAllMax(AppState& s, uint32_t now_ms) {
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    if (!ZoneActive(s, z)) continue;
    s.max_blink_until_ms[z] = now_ms + 900;
    s.max_blink_page[z] = s.page_index[z] % kPageCount;
  }
}

uint8_t GetActiveZoneCount(const AppState& state) {
  uint8_t count = 0;
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    if (ZoneActive(state, z)) ++count;
  }
  if (count == 0) count = 1;
  return count;
}

uint8_t NextZone(const AppState& state, uint8_t z) {
  for (uint8_t i = 1; i <= kMaxZones; ++i) {
    const uint8_t cand = static_cast<uint8_t>((z + i) % kMaxZones);
    if (ZoneActive(state, cand)) {
      return cand;
    }
  }
  return z;
}

PhysicalDisplayId ZoneToDisplay(const AppState& state, uint8_t z) {
  switch (state.display_topology) {
    case DisplayTopology::kLargeOnly:
      return PhysicalDisplayId::kPrimary;
    case DisplayTopology::kLargePlusSmall:
      if (z == 0 || z == 1) return PhysicalDisplayId::kPrimary;
      return PhysicalDisplayId::kSecondary;
    case DisplayTopology::kSmallOnly:
      return PhysicalDisplayId::kPrimary;
    case DisplayTopology::kDualSmall:
    case DisplayTopology::kUnconfigured:
    default:
      return (z == 0) ? PhysicalDisplayId::kPrimary : PhysicalDisplayId::kSecondary;
  }
}

uint8_t ZoneToViewportY(uint8_t z) {
  return static_cast<uint8_t>((z % kMaxZones) * 32);
}

static bool ZoneActive(const AppState& state, uint8_t z) {
  switch (state.display_topology) {
    case DisplayTopology::kSmallOnly:
      return z == 0;
    case DisplayTopology::kDualSmall:
      if (z == 0) return true;
      if (z == 2) return state.dual_screens;
      return false;
    case DisplayTopology::kLargeOnly:
      return z == 0 || z == 1;
    case DisplayTopology::kLargePlusSmall:
      if (z == 0 || z == 1) return true;
      if (z == 2) return state.dual_screens;
      return false;
    case DisplayTopology::kUnconfigured:
    default:
      return z == 0;
  }
}

bool GetPageUnits(const AppState& state, uint8_t page_index) {
  if (page_index >= kPageCount) return false;
  return (state.page_units_mask & (1U << page_index)) != 0;
}

void SetPageUnits(AppState& state, uint8_t page_index, bool imperial) {
  if (page_index >= kPageCount) return;
  if (imperial) {
    state.page_units_mask |= static_cast<uint32_t>(1U << page_index);
  } else {
    state.page_units_mask &= static_cast<uint32_t>(~(1U << page_index));
  }
}

bool GetPageMaxAlertEnabled(const AppState& state, uint8_t page_index) {
  if (page_index >= kPageCount) return false;
  return (state.page_alert_max_mask & (1U << page_index)) != 0;
}

void SetPageMaxAlertEnabled(AppState& state, uint8_t page_index, bool enabled) {
  if (page_index >= kPageCount) return;
  if (enabled) {
    state.page_alert_max_mask |= static_cast<uint32_t>(1U << page_index);
  } else {
    state.page_alert_max_mask &= static_cast<uint32_t>(~(1U << page_index));
  }
}

bool GetPageMinAlertEnabled(const AppState& state, uint8_t page_index) {
  if (page_index >= kPageCount) return false;
  return (state.page_alert_min_mask & (1U << page_index)) != 0;
}

void SetPageMinAlertEnabled(AppState& state, uint8_t page_index, bool enabled) {
  if (page_index >= kPageCount) return;
  if (enabled) {
    state.page_alert_min_mask |= static_cast<uint32_t>(1U << page_index);
  } else {
    state.page_alert_min_mask &= static_cast<uint32_t>(~(1U << page_index));
  }
}

ScreenSettings& DisplayConfigForDisplay(AppState& state, PhysicalDisplayId disp) {
  const uint8_t idx = (disp == PhysicalDisplayId::kSecondary)
                          ? static_cast<uint8_t>(1)
                          : static_cast<uint8_t>(0);
  return state.screen_cfg[idx];
}

const ScreenSettings& DisplayConfigForDisplay(const AppState& state,
                                              PhysicalDisplayId disp) {
  const uint8_t idx = (disp == PhysicalDisplayId::kSecondary)
                          ? static_cast<uint8_t>(1)
                          : static_cast<uint8_t>(0);
  return state.screen_cfg[idx];
}

ScreenSettings& DisplayConfigForZone(AppState& state, uint8_t zone) {
  return DisplayConfigForDisplay(state, ZoneToDisplay(state, zone));
}

const ScreenSettings& DisplayConfigForZone(const AppState& state,
                                           uint8_t zone) {
  return DisplayConfigForDisplay(state, ZoneToDisplay(state, zone));
}

uint8_t UiActiveUserZones(DisplayTopology topo) {
  switch (topo) {
    case DisplayTopology::kSmallOnly:
      return 1;
    case DisplayTopology::kDualSmall:
      return 2;
    case DisplayTopology::kLargeOnly:
      return 2;
    case DisplayTopology::kLargePlusSmall:
      return 3;
    case DisplayTopology::kUnconfigured:
    default:
      return 1;
  }
}

uint8_t UiUserZoneToInternal(DisplayTopology topo, uint8_t user_zone) {
  switch (topo) {
    case DisplayTopology::kSmallOnly:
      return 0;
    case DisplayTopology::kLargeOnly:
      if (user_zone == 0) return 0;
      if (user_zone == 1) return 1;
      break;
    case DisplayTopology::kDualSmall:
      if (user_zone == 0) return 0;
      if (user_zone == 1) return 2;
      break;
    case DisplayTopology::kLargePlusSmall:
      if (user_zone == 0) return 0;
      if (user_zone == 1) return 1;
      if (user_zone == 2) return 2;
      break;
    case DisplayTopology::kUnconfigured:
    default:
      if (user_zone == 0) return 0;
      break;
  }
  return 0;
}

bool IsPageHidden(const AppState& s, uint8_t page) {
  if (page >= kPageCount) return false;
  return (s.page_hidden_mask & (1U << page)) != 0;
}

uint8_t FirstVisiblePage(const AppState& s) {
  for (uint8_t p = 0; p < kPageCount; ++p) {
    if (!IsPageHidden(s, p)) return p;
  }
  return 0;
}

uint8_t NextVisiblePage(const AppState& s, uint8_t cur, int dir) {
  if (dir == 0) dir = 1;
  for (uint8_t step = 0; step < kPageCount; ++step) {
    uint8_t idx = static_cast<uint8_t>((cur + kPageCount + dir * (step + 1)) % kPageCount);
    if (!IsPageHidden(s, idx)) return idx;
  }
  return cur;
}

void EnsureVisiblePages(AppState& s) {
  const uint32_t all_mask = PageMaskAll(kPageCount);
  if ((s.page_hidden_mask & all_mask) == all_mask) {
    s.page_hidden_mask = 0;
  }
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    if (s.boot_page_index[z] >= kPageCount) {
      s.boot_page_index[z] = FirstVisiblePage(s);
    }
    if (s.page_index[z] >= kPageCount) {
      s.page_index[z] = FirstVisiblePage(s);
    }
    if (IsPageHidden(s, s.boot_page_index[z])) {
      s.boot_page_index[z] = NextVisiblePage(s, s.boot_page_index[z], +1);
    }
    if (IsPageHidden(s, s.page_index[z])) {
      s.page_index[z] = NextVisiblePage(s, s.page_index[z], +1);
    }
  }
}

DisplayTopology GetDisplayTopology(const AppState& state) {
  return state.display_topology;
}

bool SetDisplayTopology(AppState& state, DisplayTopology topo) {
  if (state.display_topology == topo) return false;
  state.display_topology = topo;
  const uint8_t zones = GetActiveZoneCount(state);
  for (uint8_t z = 0; z < zones && z < kMaxZones; ++z) {
    state.force_redraw[z] = true;
  }
  return true;
}

