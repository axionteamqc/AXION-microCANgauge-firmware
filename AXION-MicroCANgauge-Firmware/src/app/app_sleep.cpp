#include "app/app_sleep.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void AppSleepMs(uint32_t ms) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(ms));
    return;
  }
  delay(ms);
}
