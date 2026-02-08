#include <cmath>

#include "ui/edit_mode_helpers.h"
#include "app/app_globals.h"

uint8_t currentPageIndex(const AppState& state, uint8_t screen) {
  size_t page_count = 0;
  GetPageTable(page_count);
  return static_cast<uint8_t>(state.page_index[screen] % page_count);
}

PageId currentPageId(const AppState& state, uint8_t screen) {
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  return pages[currentPageIndex(state, screen)].id;
}

void persistThresholds(const AppState& state) {
  float mins[kPageCount];
  float maxs[kPageCount];
  for (size_t i = 0; i < kPageCount; ++i) {
    mins[i] = state.thresholds[i].min;
    maxs[i] = state.thresholds[i].max;
  }
  g_nvs.saveThresholds(mins, maxs, kPageCount);
}

void enterEditMode(AppState& state, uint8_t screen, const DataStore& store,
                   uint32_t now_ms) {
  PageId pid = currentPageId(state, screen);
  const PageMeta* meta = FindPageMeta(pid);
  if (!meta) return;
  const size_t page_idx = currentPageIndex(state, screen);
  EditModeState::Mode target =
      meta->has_max ? EditModeState::Mode::kEditMax
                    : (meta->has_min ? EditModeState::Mode::kEditMin
                                     : EditModeState::Mode::kNone);
  if (target == EditModeState::Mode::kNone) return;

  float canon_val = NAN;
  ScreenSettings cfg = DisplayConfigForZone(state, screen);
  cfg.imperial_units = GetPageUnits(state, page_idx);
  if (target == EditModeState::Mode::kEditMax &&
      !isnan(state.thresholds[page_idx].max)) {
    canon_val = state.thresholds[page_idx].max;
  } else if (target == EditModeState::Mode::kEditMin &&
             !isnan(state.thresholds[page_idx].min)) {
    canon_val = state.thresholds[page_idx].min;
  } else {
    PageCanonicalValue(pid, state, cfg, store, now_ms, canon_val);
  }
  if (isnan(canon_val)) canon_val = 0.0f;
  float disp = CanonToDisplay(meta->kind, canon_val, cfg);
  ThresholdGrid grid{};
  if (GetThresholdGrid(pid, cfg.imperial_units, grid)) {
    disp = SnapToGrid(disp, grid);
  }

  state.edit_mode.mode[screen] = target;
  state.edit_mode.page[screen] = static_cast<uint8_t>(page_idx);
  state.edit_mode.display_value[screen] = disp;
  state.edit_mode.last_activity_ms[screen] = now_ms;
  state.force_redraw[screen] = true;
}

void exitEditMode(AppState& state, uint8_t screen) {
  state.edit_mode.mode[screen] = EditModeState::Mode::kNone;
  // Clear locking flags for this zone.
  state.edit_mode.locked_maxmin[screen] = false;
  state.edit_mode.locked_is_max[screen] = false;
  state.force_redraw[screen] = true;
}

void saveEdit(AppState& state, uint8_t screen) {
  const uint8_t page_idx = state.edit_mode.page[screen];
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  if (page_idx >= page_count) return;
  const PageId pid = pages[page_idx].id;
  const PageMeta* meta = FindPageMeta(pid);
  if (!meta) return;
  ScreenSettings cfg = DisplayConfigForZone(state, screen);
  cfg.imperial_units = GetPageUnits(state, page_idx);
  float disp_val = state.edit_mode.display_value[screen];
  ThresholdGrid grid{};
  if (GetThresholdGrid(pid, cfg.imperial_units, grid)) {
    disp_val = SnapToGrid(disp_val, grid);
  }
  float canon = DisplayToCanon(meta->kind, disp_val, cfg);
  Thresholds& thr = state.thresholds[page_idx];
  if (state.edit_mode.mode[screen] == EditModeState::Mode::kEditMax) {
    thr.max = canon;
  } else if (state.edit_mode.mode[screen] == EditModeState::Mode::kEditMin) {
    thr.min = canon;
  }
  if (!isnan(thr.min) && !isnan(thr.max) && thr.min >= thr.max) {
    thr.min = thr.max - 0.001f;
  }
  persistThresholds(state);
  state.max_blink_until_ms[screen] = millis() + 600;
  state.max_blink_page[screen] = page_idx;
  exitEditMode(state, screen);
}
