#pragma once
// Application lifecycle: Arduino setup/loop dispatchers.
#include "app/button_task.h"

void AppSetup();
void AppLoopTick();
void ResetBaroPersist();
