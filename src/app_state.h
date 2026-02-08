#pragma once

#include <Arduino.h>

#include "app_config.h"
#include "ui_menu.h"
#include "data/datastore.h"

constexpr uint8_t kMaxZones = 3;
constexpr uint8_t kPageCount = 21;
static_assert(kPageCount <= 32, "kPageCount too large for 32-bit masks");
static_assert(kMaxZones <= 32, "kMaxZones too large for 32-bit masks");

struct MockDataStore {
  uint32_t last_update_ms = 0;
  uint16_t rpm = 0;
  uint16_t speed_tenths = 0;  // 1 decimal place
  uint16_t coolant_c = 0;
  uint16_t batt_mv = 0;       // millivolts
  bool rpm_valid = true;
  bool speed_valid = true;
  bool coolant_valid = true;
  bool batt_valid = true;

  void update(uint32_t now_ms);
};

struct SelfTestState {
  uint8_t step = 0;
  uint32_t last_step_ms = 0;
  bool request_reset_all = false;
};

struct RenderState {
  uint8_t last_page = 255;
  float last_value = 0.0f;
  bool last_valid = false;
  uint32_t last_draw_ms = 0;
  uint8_t last_blink_phase = 0;
  uint8_t last_maxblink_phase = 0;
  bool last_invert = false;
  bool last_focused = false;
  bool last_exclam_phase = false;
  bool last_lock_toast = false;
};

struct Thresholds {
  float min = NAN;
  float max = NAN;
};

struct EditModeState {
  enum class Mode : uint8_t { kNone = 0, kEditMax, kEditMin };
  Mode mode[kMaxZones] = {Mode::kNone, Mode::kNone, Mode::kNone};
  uint8_t page[kMaxZones] = {0, 0, 0};
  float display_value[kMaxZones] = {0.0f, 0.0f, 0.0f};
  uint32_t last_activity_ms[kMaxZones] = {0, 0, 0};
  bool locked_maxmin[kMaxZones] = {false, false, false};
  bool locked_is_max[kMaxZones] = {false, false, false};
};

struct LastGoodSample {
  float value = 0.0f;
  uint32_t last_ok_ms = 0;
  bool has_value = false;
};

struct ExtremaViewState {
  bool active[kMaxZones] = {false, false, false};
  bool show_min[kMaxZones] = {false, false, false};
  uint8_t page[kMaxZones] = {0, 0, 0};
};

enum class CanHealth : uint8_t {
  kOk = 0,
  kNoFrames,
  kStale,
  kDecodeBad,
  kImplausible
};

enum class CanLinkState : uint8_t {
  kNoFrames = 0,
  kNoProfileMatch,
  kDecodeInvalid,
  kOk
};

enum class UserSensorPreset : uint8_t {
  kOilPressure = 0,
  kOilTemp,
  kFuelPressure,
  kCustomVoltage,
  kCustomUnitless
};

enum class UserSensorKind : uint8_t {
  kPressure = 0,
  kTemp,
  kVoltage,
  kUnitless
};

enum class UserSensorSource : uint8_t { kSensor1 = 0, kSensor2 };

struct UserSensorCfg {
  UserSensorPreset preset = UserSensorPreset::kOilPressure;
  UserSensorKind kind = UserSensorKind::kPressure;
  UserSensorSource source = UserSensorSource::kSensor1;
  char label[8] = "OILP";
  float scale = 1.0f;
  float offset = 0.0f;
  char unit_metric[5] = "";
  char unit_imperial[5] = "";
};

struct CanLinkDiag {
  CanLinkState state = CanLinkState::kNoFrames;
  CanHealth health = CanHealth::kNoFrames;
  uint32_t last_health_change_ms = 0;
  uint32_t last_change_ms = 0;
  uint32_t window_start_ms = 0;
  uint32_t rx_total_base = 0;
  uint32_t rx_match_base = 0;
  uint32_t rx_dash_base = 0;
  uint32_t decode_oob_base = 0;
};

struct AppState {
  // Main loop owned (AppSetup/AppLoop); UI/menu reads only.
  bool oled_primary_ready = false;
  bool oled_secondary_ready = false;
  bool dual_screens = false;
  union {
    uint8_t focus_zone;
    uint8_t focus_screen;  // alias for legacy usage
  };
  bool ui_locked = false;
  bool sleep = false;
  bool demo_mode = false;
  uint32_t demo_tick_count = 0;
  bool can_ready = false;
  bool can_bitrate_locked = false;
  uint32_t can_bitrate_value = 500000;
  // CAN RX task writes; UI reads via snapshot.
  uint32_t last_can_rx_ms = 0;
  uint32_t last_can_match_ms = 0;
  bool can_safe_listen = false;
  // CAN RX task writes; UI reads.
  uint8_t id_present_mask = 0;
  bool baro_acquired = false;
  float baro_kpa = AppConfig::kDefaultBaroKpa;
  DisplayTopology display_topology = DisplayTopology::kSmallOnly;
  uint8_t display_setup_index = 0;
  char ecu_type[8] = "MS3";

  uint8_t page_index[kMaxZones] = {0, 0, 0};
  uint8_t boot_page_index[kMaxZones] = {0, 0, 0};
  uint32_t max_blink_until_ms[kMaxZones] = {0, 0, 0};
  uint8_t max_blink_page[kMaxZones] = {0, 0, 0};
  float page_recorded_max[kPageCount] = {0.0f};
  float page_recorded_min[kPageCount] = {0.0f};

  RenderState render_state[kMaxZones];
  // Indexed by physical display: 0 = primary (zones 0/1), 1 = secondary (zone 2).
  ScreenSettings screen_cfg[kMaxZones] = {
      {false, false}, {false, false}, {false, false}};

  bool reset_all_max_request = false;
  bool self_test_enabled = false;
  SelfTestState self_test;
  MockDataStore mock_data;
  UiMenu ui_menu;
  bool wizard_active = false;

  uint32_t last_input_ms = 0;
  bool force_redraw[kMaxZones] = {false, false, false};
  uint32_t last_oled_ms[kMaxZones] = {0, 0, 0};
  // Button task writes (volatile); main loop/UI reads.
  volatile bool btn_pressed = false;
  volatile uint8_t btn_pending_count = 0;
  bool allow_oled2_during_hold = false;
  bool wifi_mode_active = false;
  bool wifi_exit_confirm = false;
  uint32_t wifi_exit_confirm_until_ms = 0;
  uint32_t boot_ms = 0;

  // CAN RX task writes; UI reads via snapshot.
  struct CanStats {
    uint32_t rx_total = 0;
    uint32_t rx_match = 0;
    uint32_t rx_dash = 0;
    uint32_t rx_ok_count = 0;
    uint32_t rx_drop_count = 0;
    uint32_t rx_err_count = 0;
    uint32_t last_rx_ms = 0;
    uint32_t stale_ms = 0;
    uint32_t bus_off = 0;
    uint32_t err_passive = 0;
    uint32_t rx_overrun = 0;
    uint32_t rx_missed = 0;
    uint32_t decode_oob = 0;  // out-of-range rejects
  } can_stats;
  // CAN RX task writes; UI reads via snapshot.
  uint32_t last_bus_off_ms = 0;
  uint8_t twai_state = 0;
  uint8_t tec = 0;
  uint8_t rec = 0;
  uint32_t twai_rx_missed = 0;
  uint32_t twai_last_update_ms = 0;
  bool baro_reset_request = false;
  bool can_need_recover = false;
  uint32_t can_recover_backoff_ms = 5000;
  uint32_t can_recover_last_attempt_ms = 0;
  // Main loop owned (CAN edge/rate tracking).
  uint32_t can_edge_prev = 0;
  uint32_t can_edge_last_sample_ms = 0;
  float can_edge_rate = 0.0f;
  bool can_edge_active = false;
  struct CanRateWindow {
    uint32_t last_sample_ms = 0;
    uint32_t rx_total_prev = 0;
    uint32_t rx_match_prev = 0;
    uint32_t rx_drop_prev = 0;
    float rx_per_s = 0.0f;
    float match_per_s = 0.0f;
    float drop_per_s = 0.0f;
  } can_rates;

  // CAN RX task writes; UI reads via snapshot.
  struct CanDiag {
    uint32_t last_rx_ms = 0;
    uint32_t last_id = 0;
    uint8_t last_dlc = 0;
    uint8_t last_bytes[8] = {0};
    uint32_t per_id_rx[5] = {0, 0, 0, 0, 0};  // 0x5E8..0x5EC
  } can_diag;

  // Main loop owned (link/health evaluation); UI reads.
  CanLinkDiag can_link;

  struct OilConfig {
    enum class Mode : uint8_t { kOff = 0, kRaw = 1 };
    Mode mode = Mode::kOff;
    bool swap = false;
    char pressure_label[8] = "OilP";
    char temp_label[8] = "OilT";
  } oil_cfg;
  UserSensorCfg user_sensor[2];
  float stoich_afr = 14.7f;
  bool afr_show_lambda = false;

  Thresholds thresholds[kPageCount];
  EditModeState edit_mode;
  ExtremaViewState extrema_view;
  // UI-only best-effort cache of last good samples (from datastore) to smooth stale
  // render values; owned by the UI loop and safe to update without affecting core state.
  mutable LastGoodSample last_good[static_cast<size_t>(SignalId::kCount)];
  struct PersistAudit {
    uint32_t button_events = 0;
    uint32_t ui_writes = 0;
    uint32_t ui_write_max_ms = 0;
    uint32_t oil_writes = 0;
    uint32_t oil_write_max_ms = 0;
  } persist_audit;
  uint32_t page_units_mask = 0;  // bit=1 => imperial, 0 => metric
  uint32_t page_alert_max_mask = 0;  // bit=1 => MAX alert enabled
  uint32_t page_alert_min_mask = 0;  // bit=1 => MIN alert enabled
  uint32_t page_hidden_mask = 0;  // bit=1 => page hidden
  enum class LockToast : uint8_t { kNone = 0, kLocked, kUnlocked, kBlocked };
  LockToast lock_toast_type[kMaxZones] = {LockToast::kNone, LockToast::kNone,
                                          LockToast::kNone};
  uint32_t lock_toast_until_ms[kMaxZones] = {0, 0, 0};
};

// Helper provided in ui_render.cpp.
uint32_t pageValueRaw(const AppState& state, uint8_t page);
bool GetPageUnits(const AppState& state, uint8_t page_index);
void SetPageUnits(AppState& state, uint8_t page_index, bool imperial);
bool GetPageMaxAlertEnabled(const AppState& state, uint8_t page_index);
void SetPageMaxAlertEnabled(AppState& state, uint8_t page_index, bool enabled);
bool GetPageMinAlertEnabled(const AppState& state, uint8_t page_index);
void SetPageMinAlertEnabled(AppState& state, uint8_t page_index, bool enabled);

// Zone helpers (supports up to kMaxZones; active count currently 1-2).
uint8_t GetActiveZoneCount(const AppState& state);
uint8_t NextZone(const AppState& state, uint8_t z);
enum class PhysicalDisplayId : uint8_t { kPrimary = 0, kSecondary = 1 };
PhysicalDisplayId ZoneToDisplay(const AppState& state, uint8_t z);
uint8_t ZoneToViewportY(uint8_t z);
uint8_t UiUserZoneToInternal(DisplayTopology topo, uint8_t user_zone);
uint8_t UiActiveUserZones(DisplayTopology topo);
DisplayTopology GetDisplayTopology(const AppState& state);
bool SetDisplayTopology(AppState& state, DisplayTopology topo);
ScreenSettings& DisplayConfigForDisplay(AppState& state, PhysicalDisplayId disp);
const ScreenSettings& DisplayConfigForDisplay(const AppState& state,
                                              PhysicalDisplayId disp);
ScreenSettings& DisplayConfigForZone(AppState& state, uint8_t zone);
const ScreenSettings& DisplayConfigForZone(const AppState& state, uint8_t zone);
bool IsPageHidden(const AppState& s, uint8_t page);
uint8_t FirstVisiblePage(const AppState& s);
uint8_t NextVisiblePage(const AppState& s, uint8_t cur, int dir);
void EnsureVisiblePages(AppState& s);

void initDefaults(AppState& s);
void resetAllMax(AppState& s, uint32_t now_ms);
