#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "data/datastore.h"
#include "ui/pages.h"

enum class AlertLevel : uint8_t { kNone = 0, kWarn = 1, kCrit = 2 };

class AlertsEngine {
 public:
  AlertsEngine();

  void update(const AppState& state, const DataStore& store, uint32_t now_ms);
  AlertLevel alertForPage(PageId page) const;
  bool hasCritical() const { return has_crit_; }

 private:
  struct AlertState {
    enum class Stage : uint8_t { kDisarmed, kArmed, kPending, kActive, kClearing };
    Stage stage = Stage::kDisarmed;
    uint32_t timer_ms = 0;
    AlertLevel level = AlertLevel::kNone;
  };

  enum class Direction : uint8_t { kHigh, kLow };

  AlertState oil_p_;
  AlertState oil_t_;
  AlertState batt_;
  AlertState knk_;
  AlertLevel page_level_[kPageCount];
  bool has_crit_;

  void evalOilP(const AppState& state, const DataStore& store, uint32_t now_ms);
  void evalOilT(const AppState& state, const DataStore& store, uint32_t now_ms);
  void evalBatt(const AppState& state, const DataStore& store, uint32_t now_ms);
  void evalKnk(const AppState& state, const DataStore& store, uint32_t now_ms);
  void step(AlertState& st, bool armed, float val, float warn_on, float warn_off,
            uint32_t warn_delay, float crit_on, float crit_off,
            uint32_t crit_delay, bool latch_crit, uint32_t now_ms,
            Direction dir);
  static void resetAlert(AlertState& st);
};
