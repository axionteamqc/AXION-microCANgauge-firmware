#pragma once

#include <U8g2lib.h>
#include <cstddef>
#include <string>

class OledU8g2 {
 public:
  enum class Bus {
    kHw,
    kSw,
  };
  enum class Profile {
    kUnivision = 0,
    kWinstar = 1,
  };

  OledU8g2(Bus bus, uint8_t clock_pin, uint8_t data_pin, int8_t reset_pin);

  bool begin(uint32_t bus_hz, Profile profile, bool probe_hw = false);
  bool begin64(uint32_t bus_hz, bool probe_hw = false);
  bool isReady() const;
  void setSleep(bool sleep_on);
  void setRotation(bool flip_180);
  void setInvert(bool on);
  uint8_t height() const { return (ready_ && u8g2_) ? u8g2_->getDisplayHeight() : 0; }
  uint8_t width() const { return (ready_ && u8g2_) ? u8g2_->getDisplayWidth() : 0; }

  // Renders ultra-minimal metric layout.
  void renderMetric(const char* label, const char* big_value,
                    const char* suffix_small, const char* unit_str,
                    const char* max_str, bool valid, bool focused,
                    bool warn_marker = false, bool crit_marker = false,
                    bool crit_global = false, uint8_t viewport_y = 0,
                    uint8_t viewport_h = 0, bool clear_buffer = true,
                    bool send_buffer = true, bool xor_invert = false,
                    bool invert_zone = false);

  // Small text layout for splash / menu.
  void drawLines(const char* line1, const char* line2 = nullptr,
                 const char* line3 = nullptr, const char* line4 = nullptr);
  // Minimal passthrough helpers for boot/setup rendering.
  void simpleClear();
  void simpleSend();
  void simpleSetFontSmall();
  void simpleDrawStr(int16_t x, int16_t y, const char* s);
  void drawLoadingFrame(uint8_t progress_pct, const char* title = nullptr);
  void drawScrollingText(const char* text, const uint8_t* font, int16_t x);
  void drawCenteredText(const char* text, const uint8_t* font);
  void clearDisplay();
  uint16_t measureText(const char* text, const uint8_t* font);
  Bus bus() const { return bus_; }
  bool probeAddress();
  bool sendRawCommand(uint8_t cmd);
  void setBusClockHz(uint32_t bus_hz);
  uint32_t busClockHz() const { return bus_hz_; }

 private:
  Bus bus_;
  uint8_t clock_pin_;
  uint8_t data_pin_;
  int8_t reset_pin_;
  bool ready_;
  uint32_t bus_hz_;
  alignas(::max_align_t) uint8_t storage_[512];
  U8G2* u8g2_;
  bool invert_on_;

  void destroyDisplay();
  void createDisplay(Profile profile);
  void createDisplay64();
  bool probeHwAddress();
  bool probeSwAddress();
  bool probeBusAddress();
  void ensureFonts();
};

uint32_t GetSwI2cDelayUs();
void SetSwI2cDelayUs(uint32_t delay_us);
