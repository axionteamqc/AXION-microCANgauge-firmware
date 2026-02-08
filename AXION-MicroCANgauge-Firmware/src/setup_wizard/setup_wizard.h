#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "app_config.h"

#if SETUP_WIZARD_ENABLED
#include "can_link/can_autobaud.h"
#include "can_link/twai_link.h"
#include "data/datastore.h"
#include "drivers/oled_u8g2.h"
#include "settings/nvs_store.h"
#include "ui_menu.h"

class SetupWizard {
 public:
  SetupWizard(TwaiLink& link, DataStore& store, NvsStore& nvs);

  void begin(uint8_t focus_screen, AppState& state);
  void handleAction(UiAction action, AppState& state);
  void tick(AppState& state, uint32_t now_ms);
  void render(const AppState& state, OledU8g2& oled_primary,
              OledU8g2& oled_secondary, uint32_t now_ms);
  bool isActive() const;

 private:
  enum class Phase : uint8_t {
    kInactive = 0,
    kKoeoIntro,
    kKoeoScanMenu,        // user chooses auto-scan vs manual
    kKoeoScanRun,         // advanced auto-detect running
    kKoeoScanLocked,      // auto-scan found a bitrate
    kKoeoScanSaved,       // saved + reboot pending
    kKoeoValidateBitrate, // manual validate current bitrate
    kKoeoManualBitrate,   // manual selection of bitrate
    kKoeoMeasurePeriod,
    kKoeoBaro,
    kKoeoValidate,
    kKoeoDone,
    kRunIntro,
    kRunCapture,
    kRunValidate,
    kSetupDone,
    kKoeoFail,
    kRunFail
  };

  enum class FailReason : uint8_t {
    kNone = 0,
    kNoFrames,
    kCanStartFail,
    kLowScore,
    kBusError,
    kProfileMismatch,
  };

  struct IntervalBuf {
    uint32_t last_ts = 0;
    uint16_t deltas[16] = {0};
    uint8_t count = 0;
  };

  struct SampleBuf {
    uint32_t ts = 0;
    float val = 0.0f;
  };

  void changePhase(Phase p, uint32_t now_ms);
  void drainCan(AppState& state, uint32_t now_ms);
  void recordInterval(uint8_t idx, uint32_t ts_ms);
  bool intervalsReady(uint8_t idx) const;
  uint32_t medianInterval(uint8_t idx) const;
  bool allIntervalsDone(uint8_t mask) const;
  void finalizeIntervals(AppState& state);
  void stopCan(AppState& state);

  void resetBaroBuffers();
  void handleBaro(uint32_t now_ms, AppState& state);
  float computeSigma() const;
  float computeMean() const;

  void resetRunBuffers();
  void handleRun(uint32_t now_ms);
  void pushSample(SampleBuf* buf, uint8_t capacity, uint8_t& head,
                  uint8_t& count, uint32_t ts, float val);
  bool computeDelta(const SampleBuf* buf, uint8_t count, uint32_t now_ms,
                    float& delta, uint32_t window_ms) const;
  void recordDebug(const twai_message_t& msg, uint32_t now_ms);

  void renderFocused(const AppState& state, OledU8g2& disp, uint32_t now_ms);
  void saveProgressKoeo(AppState& state, bool mark_done);
  void saveProgressRun(AppState& state);

  TwaiLink& twai_;
  DataStore& store_;
  NvsStore& nvs_;

  Phase phase_;
  uint32_t phase_start_ms_;
  uint8_t focus_screen_;
  bool scan_attempted_;
  bool intervals_done_;
  bool baro_acquired_;
  float baro_kpa_;
  uint16_t stale_ms_[5];
  IntervalBuf intervals_[5];
  AutoBaudStats last_scan_;
  AutoBaudDiag last_diag_;
  FailReason fail_reason_ = FailReason::kNone;

  float last_map_kpa_;
  float last_rpm_;
  float last_tps_;
  uint32_t last_map_ms_;
  uint32_t last_rpm_ms_;
  uint32_t last_tps_ms_;
  bool blip_detected_;
  uint8_t scan_rate_idx_ = 0;
  uint32_t locked_rate_ = 0;

  // Baro buffers
  float map_samples_[20];
  uint8_t map_count_;
  uint8_t map_head_;
  uint32_t stable_start_ms_;
  bool stable_cond_;
  uint32_t baro_avg_start_ms_;
  float baro_sum_;
  uint16_t baro_count_;

  // Run blip buffers
  SampleBuf rpm_buf_[24];
  SampleBuf map_buf_[24];
  uint8_t rpm_count_;
  uint8_t map_count_run_;
  uint8_t rpm_head_;
  uint8_t map_head_run_;

  // Validation live counters
  bool validate_active_ = false;
  uint32_t validate_start_ms_ = 0;
  uint32_t validate_window_ms_ = 0;
  uint32_t validate_rx_total_ = 0;
  uint32_t validate_rx_dash_ = 0;
  uint32_t validate_err_bus_off_ = 0;
  uint32_t validate_err_ep_ = 0;
  uint32_t validate_err_ov_ = 0;
  uint32_t validate_err_missed_ = 0;
  uint32_t validate_rate_ = 0;

  // Diagnostics last frames per ID range 0..4 (0x5E8..0x5EC).
  uint8_t diag_last_bytes_[5][8]{};
  uint8_t diag_last_dlc_[5]{};
  uint32_t diag_rx_counts_[5]{};
  uint32_t diag_last_rx_ms_[5]{};

  // RAW CAN proof (debug)
  bool debug_view_ = false;
  uint32_t debug_last_id_ = 0;
  uint8_t debug_last_dlc_ = 0;
  uint32_t debug_rx_total_ = 0;
  uint32_t debug_fps_counter_ = 0;
  uint32_t debug_fps_ = 0;
  uint32_t debug_last_fps_ts_ = 0;
};
#endif  // SETUP_WIZARD_ENABLED
