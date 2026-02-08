#pragma once

#include <stdint.h>

void StartButtonTask();
uint32_t ButtonTaskWatermark();
void flushButtonQueue();
