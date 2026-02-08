#include "drivers/oled_u8g2.h"

#include <Arduino.h>
#include <Wire.h>
#include <cstring>

#include "app_config.h"
#include "config/factory_config.h"

namespace {
constexpr uint8_t kI2cAddress = 0x3C << 1;  // U8G2 uses 8-bit addr

// Draw an alert triangle with an exclamation point. x_right is the right edge
// of the icon; y_top is the top; height controls size. draw_black forces
// drawing in black (useful when the buffer is inverted via XOR).
void DrawAlertTriangle(U8G2* u8, int16_t x_right, int16_t y_top,
                       int16_t height, bool draw_black) {
  if (height < 10) height = 10;
  const int16_t base_y = static_cast<int16_t>(y_top + height);
  const int16_t apex_x = static_cast<int16_t>(x_right - height / 2);
  const int16_t left_x = static_cast<int16_t>(x_right - height);
  const int16_t right_x = x_right;
  // Filled triangle
  u8->setDrawColor(draw_black ? 0 : 1);
  for (int16_t y = y_top; y <= base_y; ++y) {
    const int16_t dy = static_cast<int16_t>(y - y_top);
    const int16_t cur_left =
        static_cast<int16_t>(apex_x -
                             ((apex_x - left_x) * dy) / (height == 0 ? 1 : height));
    const int16_t cur_right =
        static_cast<int16_t>(apex_x +
                             ((right_x - apex_x) * dy) / (height == 0 ? 1 : height));
    u8->drawLine(cur_left, y, cur_right, y);
  }
  // Exclamation mark in white for contrast
  u8->setDrawColor(draw_black ? 1 : 0);
  const int16_t mark_x = apex_x;
  const int16_t mark_top = static_cast<int16_t>(y_top + height / 3 - 2);
  // Short stroke and clear gap before the dot.
  const int16_t mark_bottom = static_cast<int16_t>(base_y - 11);
  u8->drawLine(mark_x, mark_top, static_cast<int16_t>(mark_x), mark_bottom);
  u8->drawLine(static_cast<int16_t>(mark_x - 1), mark_top,
               static_cast<int16_t>(mark_x - 1), mark_bottom);
  u8->drawLine(static_cast<int16_t>(mark_x + 1), mark_top,
               static_cast<int16_t>(mark_x + 1), mark_bottom);
  u8->drawDisc(mark_x, static_cast<int16_t>(base_y - 4), 2, U8G2_DRAW_ALL);
  u8->setDrawColor(1);
}
}

OledU8g2::OledU8g2(Bus bus, uint8_t clock_pin, uint8_t data_pin,
                   int8_t reset_pin)
    : bus_(bus),
      clock_pin_(clock_pin),
      data_pin_(data_pin),
      reset_pin_(reset_pin),
      ready_(false),
      u8g2_(nullptr),
      invert_on_(false) {}

void OledU8g2::destroyDisplay() {
  if (u8g2_) {
    u8g2_->~U8G2();
    u8g2_ = nullptr;
  }
}

bool OledU8g2::begin(uint32_t bus_hz, Profile profile, bool probe_hw) {
  ready_ = false;
  destroyDisplay();
  if (probe_hw && !probeHwAddress()) {
    return false;
  }

  createDisplay(profile);
  if (!u8g2_) {
    return false;
  }

  u8g2_->setI2CAddress(kI2cAddress);
  if (bus_hz > 0) {
    u8g2_->setBusClock(bus_hz);
  }

  u8g2_->begin();
  u8g2_->setPowerSave(0);
  invert_on_ = false;
  ready_ = true;
  return ready_;
}

bool OledU8g2::begin64(uint32_t bus_hz, bool probe_hw) {
  ready_ = false;
  destroyDisplay();
  if (probe_hw && !probeHwAddress()) {
    return false;
  }
  createDisplay64();
  if (!u8g2_) {
    return false;
  }
  u8g2_->setI2CAddress(kI2cAddress);
  if (bus_hz > 0) {
    u8g2_->setBusClock(bus_hz);
  }
  u8g2_->begin();
  u8g2_->setPowerSave(0);
  invert_on_ = false;
  ready_ = true;
  return ready_;
}

bool OledU8g2::isReady() const {
  return ready_;
}

void OledU8g2::setSleep(bool sleep_on) {
  if (!ready_) {
    return;
  }
  u8g2_->setPowerSave(sleep_on ? 1 : 0);
}

void OledU8g2::setRotation(bool flip_180) {
  if (!ready_) {
    return;
  }
  u8g2_->setDisplayRotation(flip_180 ? U8G2_R2 : U8G2_R0);
}

void OledU8g2::setInvert(bool on) {
  if (!ready_) {
    return;
  }
  if (invert_on_ == on) {
    return;
  }
  u8g2_->sendF("c", on ? 0xA7 : 0xA6);
  invert_on_ = on;
}

void OledU8g2::ensureFonts() {
  // No-op placeholder: fonts are set per draw below.
}

void OledU8g2::createDisplay(Profile profile) {
  destroyDisplay();
  if (bus_ == Bus::kHw) {
    if (profile == Profile::kUnivision) {
      u8g2_ = new (storage_)
          U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C(U8G2_R0, reset_pin_);
    } else {
      u8g2_ = new (storage_)
          U8G2_SSD1306_128X32_WINSTAR_F_HW_I2C(U8G2_R0, reset_pin_);
    }
  } else {
    if (profile == Profile::kUnivision) {
      u8g2_ = new (storage_) U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C(
          U8G2_R0, clock_pin_, data_pin_, reset_pin_);
    } else {
      u8g2_ = new (storage_) U8G2_SSD1306_128X32_WINSTAR_F_SW_I2C(
          U8G2_R0, clock_pin_, data_pin_, reset_pin_);
    }
  }
}

void OledU8g2::createDisplay64() {
  destroyDisplay();
  if (bus_ == Bus::kHw) {
    u8g2_ = new (storage_) U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, reset_pin_);
  } else {
    u8g2_ = new (storage_) U8G2_SSD1306_128X64_NONAME_F_SW_I2C(U8G2_R0, clock_pin_,
                                                               data_pin_, reset_pin_);
  }
}

bool OledU8g2::probeHwAddress() {
  if (bus_ != Bus::kHw) {
    return true;
  }
  Wire.beginTransmission(0x3C);
  return Wire.endTransmission() == 0;
}

void OledU8g2::renderMetric(const char* label, const char* value_str,
                            const char* suffix_small, const char* unit_str,
                            const char* max_str, bool valid, bool focused,
                            bool warn_marker, bool crit_marker, bool crit_global,
                            uint8_t viewport_y, uint8_t viewport_h,
                            bool clear_buffer, bool send_buffer,
                            bool xor_invert, bool invert_zone) {
  if (!ready_) {
    return;
  }
  const bool has_error_value =
      (!valid) && (value_str != nullptr) && (value_str[0] != '\0');
  const char* value = has_error_value ? value_str : (valid ? value_str : "---");
  const char* suffix_line = (suffix_small && suffix_small[0] != '\0') ? suffix_small : "";
  const char* label_line = label ? label : "";
  const char* unit_line = unit_str ? unit_str : "";

  if (clear_buffer) {
    u8g2_->clearBuffer();
  }
  u8g2_->setDrawColor(crit_global ? 2 : 1);

  // Big centered value with auto-fit fonts
  const uint8_t display_w = u8g2_->getDisplayWidth();
  const uint8_t effective_h =
      (viewport_h > 0 && viewport_h < u8g2_->getDisplayHeight())
          ? viewport_h
          : u8g2_->getDisplayHeight();
  if (invert_zone) {
    u8g2_->setDrawColor(1);
    u8g2_->drawBox(0, viewport_y, display_w, effective_h);
    u8g2_->setDrawColor(0);
  }
  const uint8_t* fonts[] = {
      u8g2_font_inb33_mn, u8g2_font_logisoso34_tn, u8g2_font_logisoso32_tn,
      u8g2_font_logisoso30_tn};
  const uint8_t* error_fonts[] = {u8g2_font_logisoso20_tf, u8g2_font_6x12_tr};
  // Reorder based on config preference
  uint8_t order[4] = {0, 1, 2, 3};
  switch (AppConfig::kBigValueFont) {
    case AppConfig::BigValueFont::kInb33:
      order[0] = 0;
      order[1] = 1;
      order[2] = 2;
      order[3] = 3;
      break;
    case AppConfig::BigValueFont::kLogiso34:
      order[0] = 1;
      order[1] = 2;
      order[2] = 3;
      order[3] = 0;
      break;
    case AppConfig::BigValueFont::kLogiso32:
      order[0] = 2;
      order[1] = 3;
      order[2] = 1;
      order[3] = 0;
      break;
  }

  const uint8_t* chosen_font = error_fonts[1];
  if (has_error_value) {
    for (uint8_t i = 0; i < 2; ++i) {
      u8g2_->setFont(error_fonts[i]);
      int16_t ascent = u8g2_->getAscent();
      int16_t descent = u8g2_->getDescent();
      int16_t h = ascent - descent;
      int16_t w = u8g2_->getStrWidth(value);
      if (h <= effective_h && w <= display_w) {
        chosen_font = error_fonts[i];
        break;
      }
    }
  } else {
    uint8_t chosen = order[3];  // fallback to smallest in order
    for (uint8_t i = 0; i < 4; ++i) {
      const uint8_t idx = order[i];
      u8g2_->setFont(fonts[idx]);
      int16_t ascent = u8g2_->getAscent();
      int16_t descent = u8g2_->getDescent();
      int16_t h = ascent - descent;
      int16_t w = u8g2_->getStrWidth(value);
      if (h <= effective_h && w <= display_w) {
        chosen = idx;
        break;
      }
    }
    chosen_font = fonts[chosen];
  }

  u8g2_->setFont(chosen_font);
  int16_t ascent = u8g2_->getAscent();
  int16_t descent = u8g2_->getDescent();
  int16_t w = u8g2_->getStrWidth(value);
  const int16_t baseline_base = (effective_h - 1) + descent;  // bottom-safe baseline
  const int8_t nudge = has_error_value ? AppConfig::kErrorValueBaselineNudgePx
                                       : AppConfig::kBigValueBaselineNudgePx;
  int16_t baseline_y = static_cast<int16_t>(baseline_base + nudge);
  const int16_t y_min = ascent;
  const int16_t y_max_relaxed =
      baseline_base + AppConfig::kBigValueBaselineMaxExtraPx;
  if (baseline_y < y_min) baseline_y = y_min;
  if (baseline_y > y_max_relaxed) baseline_y = y_max_relaxed;
  int16_t x = (display_w - w) / 2;
  if (strcmp(label_line, "BATT") == 0) {
    x += 4;
    if (x < 0) x = 0;
    if (x > display_w - w) x = display_w - w;
  }
  if (x < 0) x = 0;
  u8g2_->setFont(chosen_font);
  const int16_t big_value_y =
      static_cast<int16_t>(baseline_y + 1 + static_cast<int16_t>(viewport_y));  // UX tweak
  u8g2_->drawStr(x, big_value_y, value);
  // Small elements
  u8g2_->setFont(u8g2_font_6x12_tr);
  const int16_t label_y = 11 + static_cast<int16_t>(viewport_y) - 2;
  if (strcmp(label_line, "AFR TG") == 0) {
    u8g2_->drawStr(0 + 1, 11 + static_cast<int16_t>(viewport_y) - 2, "AFR");
    u8g2_->drawStr(0 + 1, 23 + static_cast<int16_t>(viewport_y) - 2, "TG");
  } else {
    u8g2_->drawStr(0 + 1, label_y, label_line);
  }

  if (max_str && max_str[0] != '\0') {
    int16_t mw = u8g2_->getStrWidth(max_str);
    int16_t mx = static_cast<int16_t>(u8g2_->getDisplayWidth() - mw - 1);
    if (mx < 0) mx = 0;
    const int16_t my = static_cast<int16_t>(viewport_y + 10);
    u8g2_->drawStr(mx, my, max_str);
  }

  // Units: always rendered; in alert draw in opposite color so they stay visible on inverted bg.
  if (unit_line && unit_line[0] != '\0') {
    char small_line[16];
    small_line[0] = '\0';
    if (suffix_line[0] != '\0') {
      strlcpy(small_line, suffix_line, sizeof(small_line));
      strlcat(small_line, unit_line, sizeof(small_line));
    } else {
      strlcpy(small_line, unit_line, sizeof(small_line));
    }
    int16_t uw = u8g2_->getStrWidth(small_line);
    int16_t ux = u8g2_->getDisplayWidth() - uw - 1;
    if (ux < 0) ux = 0;
    int16_t uy = static_cast<int16_t>(viewport_y + effective_h - 1);
    if (strcmp(label_line, "ADV") == 0) {
      uy -= 5;
    }
    // Draw units; on inverted backgrounds, draw as "off pixels" to stay visible.
    const bool draw_units_black = (xor_invert && !invert_zone) || invert_zone;
    u8g2_->setDrawColor(draw_units_black ? 0 : 1);
    u8g2_->drawStr(ux, uy, small_line);
    u8g2_->setDrawColor(1);
  }

  if (focused) {
    u8g2_->drawBox(0, static_cast<int16_t>(viewport_y + effective_h - 3), 3, 3);
  }

  if (xor_invert && !invert_zone) {
    u8g2_->setDrawColor(2);
    u8g2_->drawBox(0, viewport_y, display_w, effective_h);
    u8g2_->setDrawColor(1);
  }
  if (invert_zone) {
    u8g2_->setDrawColor(1);
  }
  if (warn_marker || crit_marker) {
    int16_t icon_h = static_cast<int16_t>(std::min<int16_t>(
        static_cast<int16_t>(28), static_cast<int16_t>(effective_h - 2)));
    if (icon_h < 12) icon_h = 12;
    const int16_t icon_x_right = static_cast<int16_t>(display_w - 2);
    int16_t icon_y_top = static_cast<int16_t>(viewport_y + 1);
    const int16_t max_y = static_cast<int16_t>(viewport_y + effective_h - 1);
    if (icon_y_top + icon_h > max_y) {
      icon_y_top = static_cast<int16_t>(max_y - icon_h);
      if (icon_y_top < static_cast<int16_t>(viewport_y)) {
        icon_y_top = static_cast<int16_t>(viewport_y);
      }
    }
    if (icon_h >= 1) {
      const bool draw_black = (xor_invert && !invert_zone) || invert_zone;
      // Fill triangle
      DrawAlertTriangle(u8g2_, icon_x_right, icon_y_top, icon_h, draw_black);
      // Exclamation in opposite color for visibility
      const int16_t base_y = static_cast<int16_t>(icon_y_top + icon_h);
      const int16_t apex_x = static_cast<int16_t>(icon_x_right - icon_h / 2);
      u8g2_->setDrawColor(draw_black ? 1 : 0);
      int16_t mark_top = static_cast<int16_t>(icon_y_top + icon_h / 3 - 2);
      int16_t mark_bottom = static_cast<int16_t>(base_y - 11);
      u8g2_->drawLine(apex_x, mark_top, apex_x, mark_bottom);
      u8g2_->drawDisc(apex_x, static_cast<int16_t>(base_y - 4), 2, U8G2_DRAW_ALL);
      u8g2_->setDrawColor(1);
    }
  }

  if (send_buffer) {
    u8g2_->sendBuffer();
  }
}

void OledU8g2::drawLines(const char* line1, const char* line2,
                         const char* line3, const char* line4) {
  if (!ready_) {
    return;
  }
  u8g2_->clearBuffer();
  u8g2_->setFont(u8g2_font_6x12_tr);
  const char* lines[] = {line1, line2, line3, line4};
  int16_t y = 12;  // baseline for first line
  for (uint8_t i = 0; i < 4; ++i) {
    if (lines[i]) {
      u8g2_->drawStr(0, y, lines[i]);
      y += 12;  // step to next baseline
      if (y >= static_cast<int16_t>(u8g2_->getDisplayHeight())) {
        break;
      }
    }
  }
  u8g2_->sendBuffer();
}

void OledU8g2::simpleClear() {
  if (ready_) {
    u8g2_->clearBuffer();
  }
}

void OledU8g2::simpleSend() {
  if (ready_) {
    u8g2_->sendBuffer();
  }
}

void OledU8g2::simpleSetFontSmall() {
  if (ready_) {
    u8g2_->setFont(u8g2_font_6x12_tr);
  }
}

void OledU8g2::simpleDrawStr(int16_t x, int16_t y, const char* s) {
  if (ready_) {
    u8g2_->drawStr(x, y, s);
  }
}

void OledU8g2::drawLoadingFrame(uint8_t progress_pct, const char* title) {
  if (!ready_) {
    return;
  }
  const uint8_t pct = (progress_pct > 100) ? 100 : progress_pct;
  u8g2_->clearBuffer();
  u8g2_->setFont(u8g2_font_logisoso20_tf);
  if (!title) {
    title = kBootBrandText;
  }
  int16_t ascent = u8g2_->getAscent();
  int16_t descent = u8g2_->getDescent();
  int16_t h = ascent - descent;
  int16_t top = (u8g2_->getDisplayHeight() - h) / 2 - 2;  // nudge up
  if (top < 0) top = 0;
  int16_t baseline = top + ascent;
  int16_t tw = u8g2_->getStrWidth(title);
  int16_t tx = (u8g2_->getDisplayWidth() - tw) / 2;
  if (tx < 0) tx = 0;
  u8g2_->drawStr(tx, baseline, title);
  const uint8_t bar_height = 6;
  const uint8_t bar_y = u8g2_->getDisplayHeight() - (bar_height + 2);
  const uint8_t bar_width = u8g2_->getDisplayWidth();
  u8g2_->drawFrame(0, bar_y, bar_width, bar_height);
  const uint8_t inner_width =
      static_cast<uint8_t>((pct * (bar_width > 4 ? bar_width - 4 : bar_width)) / 100);
  if (inner_width > 0) {
    u8g2_->drawBox(2, bar_y + 2, inner_width, (bar_height > 4) ? bar_height - 4 : bar_height);
  }
  u8g2_->sendBuffer();
}

void OledU8g2::drawScrollingText(const char* text, const uint8_t* font,
                                 int16_t x) {
  if (!ready_ || text == nullptr) {
    return;
  }
  u8g2_->clearBuffer();
  u8g2_->setFont(font);
  int16_t ascent = u8g2_->getAscent();
  int16_t descent = u8g2_->getDescent();
  int16_t h = ascent - descent;
  int16_t top = (u8g2_->getDisplayHeight() - h) / 2;
  int16_t baseline = top + ascent;
  int16_t y_min = ascent;
  int16_t y_max = (u8g2_->getDisplayHeight() - 1) + descent;
  if (baseline < y_min) baseline = y_min;
  if (baseline > y_max) baseline = y_max;
  u8g2_->drawStr(x, baseline, text);
  u8g2_->sendBuffer();
}

uint16_t OledU8g2::measureText(const char* text, const uint8_t* font) {
  if (!ready_ || text == nullptr) {
    return 0;
  }
  u8g2_->setFont(font);
  return static_cast<uint16_t>(u8g2_->getStrWidth(text));
}

void OledU8g2::drawCenteredText(const char* text, const uint8_t* font) {
  if (!ready_ || text == nullptr) {
    return;
  }
  u8g2_->clearBuffer();
  u8g2_->setFont(font);
  int16_t ascent = u8g2_->getAscent();
  int16_t descent = u8g2_->getDescent();
  int16_t h = ascent - descent;
  int16_t baseline = ((u8g2_->getDisplayHeight() - h) / 2) + ascent;
  int16_t w = u8g2_->getStrWidth(text);
  int16_t x = (u8g2_->getDisplayWidth() - w) / 2;
  if (x < 0) x = 0;
  u8g2_->drawStr(x, baseline, text);
  u8g2_->sendBuffer();
}

void OledU8g2::clearDisplay() {
  if (!ready_) {
    return;
  }
  u8g2_->clearBuffer();
  u8g2_->sendBuffer();
}
