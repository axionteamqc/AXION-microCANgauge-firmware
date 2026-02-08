#include <Arduino.h>

#include "app/button_task.h"
#include "app/app_globals.h"
#include "app/app_sleep.h"
#include "app/input_runtime.h"
#include "app_config.h"
#include "config/factory_config.h"
#include "config/logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pins.h"

static TaskHandle_t g_btn_task = nullptr;

template <typename F>
static inline void WithStateLock(F&& fn) {
  portENTER_CRITICAL(&g_state_mux);
  fn();
  portEXIT_CRITICAL(&g_state_mux);
}

uint32_t ButtonTaskWatermark() {
  if (!g_btn_task) return 0;
  return uxTaskGetStackHighWaterMark(g_btn_task);
}

void flushButtonQueue() {
  if (!g_btnQueue) {
    return;
  }
  xQueueReset(g_btnQueue);
  WithStateLock([&]() {
    g_state.btn_pending_count = 0;
    g_state.btn_pressed = false;
  });
}

static void ButtonTask(void* arg) {
  (void)arg;
  constexpr uint8_t kIntegratorMax = 20;  // ~20 ms at 1 kHz
  uint8_t counter = kIntegratorMax;
  bool debounced_pressed = false;
  bool prev_debounced_pressed = false;
  uint8_t click_count = 0;
  uint32_t pressed_since_ms = 0;
  uint32_t last_release_ms = 0;
  bool long_emitted = false;
  uint8_t lock_clicks = 0;
  uint32_t lock_window_start_ms = 0;
  bool lock_armed = false;
  bool failsafe_pending = false;

  pinMode(Pins::kButton, INPUT_PULLUP);
  for (;;) {
    const uint32_t now_ms = millis();
    const bool raw_pressed = digitalRead(Pins::kButton) == LOW;
    if (raw_pressed) {
      if (counter > 0) counter--;
    } else {
      if (counter < kIntegratorMax) counter++;
    }

    if (counter == 0) {
      debounced_pressed = true;
    } else if (counter == kIntegratorMax) {
      debounced_pressed = false;
    }

    if (debounced_pressed && !prev_debounced_pressed) {
      pressed_since_ms = now_ms;
      long_emitted = false;
      WithStateLock([&]() {
        g_state.btn_pressed = true;
        g_state.last_input_ms = now_ms;
      });
      lock_armed = (lock_clicks == 3);
      failsafe_pending = false;
    }

    if (!debounced_pressed && prev_debounced_pressed) {
      WithStateLock([&]() {
        g_state.btn_pressed = false;
      });
      const uint32_t dur = now_ms - pressed_since_ms;
      if (failsafe_pending) {
        // Already handled by restart path; ignore clicks.
        click_count = 0;
        WithStateLock([&]() {
          g_state.btn_pending_count = 0;
        });
        failsafe_pending = false;
      }
      if (long_emitted) {
        click_count = 0;
        WithStateLock([&]() {
          g_state.btn_pending_count = 0;
        });
        long_emitted = false;
        lock_armed = false;
      } else if (dur >= AppConfig::kButtonMinPressMs) {
        const uint32_t lock_gap =
            (lock_window_start_ms == 0) ? 0 : (now_ms - lock_window_start_ms);
        if (lock_window_start_ms == 0 || lock_gap > 1200U) {
          lock_clicks = 0;
          lock_window_start_ms = now_ms;
        }
        if (lock_armed && dur >= 2000U) {
          UiAction act =
              (dur >= 3000U) ? UiAction::kUnlockGesture : UiAction::kLockGesture;
          BtnMsg msg{act, now_ms};
          xQueueSend(g_btnQueue, &msg, 0);
          lock_clicks = 0;
          lock_window_start_ms = 0;
          lock_armed = false;
          click_count = 0;
          WithStateLock([&]() {
            g_state.btn_pending_count = 0;
          });
          continue;
        }
        if (!lock_armed) {
          ++lock_clicks;
        }
        ++click_count;
        last_release_ms = now_ms;
        WithStateLock([&]() {
          g_state.btn_pending_count = click_count;
          g_state.last_input_ms = now_ms;
        });
      }
    }

    if (debounced_pressed && !long_emitted &&
        (now_ms - pressed_since_ms) >= AppConfig::kButtonLongPressMs) {
      if (!lock_armed) {
        UiAction act =
            (click_count == 0) ? UiAction::kLong : UiAction::kClick1Long;
        BtnMsg msg{act, now_ms};
        xQueueSend(g_btnQueue, &msg, 0);
        click_count = 0;
        WithStateLock([&]() {
          g_state.btn_pending_count = 0;
          g_state.last_input_ms = now_ms;
        });
        long_emitted = true;
      }
    }

    // Fail-safe restart: long hold beyond threshold (independent of long press action).
    if (debounced_pressed && !failsafe_pending && kFailsafeRestartHoldMs > 0 &&
        (now_ms - pressed_since_ms) >= kFailsafeRestartHoldMs) {
      failsafe_pending = true;
      if (kEnableVerboseSerialLogs) {
        LOGW("FAILSAFE: button held, restarting...\r\n");
      }
      // Clear any pending click state to avoid accidental actions.
      click_count = 0;
      WithStateLock([&]() {
        g_state.btn_pending_count = 0;
        g_state.btn_pressed = false;
        g_state.last_input_ms = now_ms;
      });
      AppSleepMs(50);
      ESP.restart();
    }

    if (!debounced_pressed && click_count > 0) {
      const uint32_t elapsed = now_ms - last_release_ms;
      if (elapsed >= AppConfig::kButtonMultiClickWindowMs ||
          (click_count >= 5 && elapsed >= 250)) {
        UiAction act = UiAction::kNone;
        switch (click_count) {
          case 1:
            act = UiAction::kClick1;
            break;
          case 2:
            act = UiAction::kClick2;
            break;
          case 3:
            act = UiAction::kClick3;
            break;
          case 4:
            act = UiAction::kClick4;
            break;
          case 5:
            act = UiAction::kClick5;
            break;
          case 6:
            act = UiAction::kClick6;
            break;
          case 7:
            act = UiAction::kClick7;
            break;
          default:
            break;
        }
        if (act != UiAction::kNone) {
          BtnMsg msg{act, now_ms};
          xQueueSend(g_btnQueue, &msg, 0);
          WithStateLock([&]() {
            g_state.last_input_ms = now_ms;
          });
        }
        click_count = 0;
        WithStateLock([&]() {
          g_state.btn_pending_count = 0;
        });
      }
    }

    prev_debounced_pressed = debounced_pressed;
    vTaskDelay(1);
  }
}

void StartButtonTask() {
  g_btnQueue = xQueueCreate(8, sizeof(BtnMsg));
  if (g_btnQueue != nullptr) {
#if defined(CONFIG_FREERTOS_UNICORE)
    xTaskCreate(ButtonTask, "btn", 2048, nullptr, 1, &g_btn_task);
#else
    xTaskCreatePinnedToCore(ButtonTask, "btn", 2048, nullptr, 1, &g_btn_task, 0);
#endif
  }
}
