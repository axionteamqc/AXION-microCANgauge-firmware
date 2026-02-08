#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "data/datastore.h"

enum class PageId : uint8_t {
  kOilP = 0,
  kOilT,
  kBoost,
  kMapAbs,
  kRpm,
  kClt,
  kMat,
  kBatt,
  kTps,
  kAdv,
  kAfr1,
  kAfrTgt,
  kKnk,
  kVss,
  kEgt1,
  kPw1,
  kPw2,
  kPwSeq,
  kEgo,
  kLaunch,
  kTc,
  kCount
};

struct PageRenderData {
  const char* label = "";
  char big[16] = "";
  char suffix[8] = "";
  const char* unit = "";
  float canon_value = 0.0f;
  bool has_canon = false;
  bool valid = false;
  bool has_error = false;
  char err_a[5] = "";
  char err_b[5] = "";
  bool blink = false;
};

struct PageDef {
  PageId id;
  const char* label;
};

enum class ValueKind : uint8_t {
  kPressure,
  kBoost,
  kTemp,
  kVoltage,
  kRpm,
  kPercent,
  kSpeed,
  kDeg,
  kAfr,
  kNone
};

struct PageMeta {
  PageId id;
  const char* label;
  ValueKind kind;
  bool has_min;
  bool has_max;
};

struct ThresholdGrid {
  float min_disp;
  float max_disp;
  float step_disp;
  uint8_t decimals;
};

const PageDef* GetPageTable(size_t& count);
const PageMeta* GetPageMeta(size_t& count);
const PageMeta* FindPageMeta(PageId id);
PageRenderData BuildPageData(PageId id, const AppState& state,
                             const ScreenSettings& cfg, const DataStore& store,
                             uint32_t now_ms);
bool PageCanonicalValue(PageId id, const AppState& state,
                        const ScreenSettings& cfg, const DataStore& store,
                        uint32_t now_ms, float& out);
float ThresholdStep(ValueKind kind);
float CanonToDisplay(ValueKind kind, float canon, const ScreenSettings& cfg);
float DisplayToCanon(ValueKind kind, float display, const ScreenSettings& cfg);
bool GetThresholdGrid(PageId id, bool imperial, ThresholdGrid& out);
bool GetThresholdGridWithState(PageId id, const AppState& state, bool imperial,
                               ThresholdGrid& out);
float SnapToGrid(float disp, const ThresholdGrid& g);
bool IsOnGrid(float disp, const ThresholdGrid& g);
