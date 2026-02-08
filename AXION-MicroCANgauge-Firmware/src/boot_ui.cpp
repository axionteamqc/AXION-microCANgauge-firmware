#include "boot_ui.h"

#include <Arduino.h>

#include "boot/boot_strings.h"
#include "config/factory_config.h"

void showSoarerProgress(OledU8g2& oled1, uint8_t progress_pct) {
  oled1.drawLoadingFrame(progress_pct, BootBrandText());
}

void playHelloSequence(OledU8g2& oled1) {
  const uint8_t* font = u8g2_font_logisoso20_tf;
  const char* a = BootHello1();
  const char* b = BootHello2();
  if (a && a[0] != '\0') {
    oled1.clearDisplay();
    oled1.drawCenteredText(a, font);
    delay(900);
  }
  if (b && b[0] != '\0') {
    oled1.clearDisplay();
    oled1.drawCenteredText(b, font);
    delay(900);
  }
  oled1.clearDisplay();
}
