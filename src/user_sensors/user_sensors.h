#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "data/datastore.h"

SignalId sourceToSignal(UserSensorSource src);
UserSensorKind presetToKind(UserSensorPreset preset);
const char* defaultLabel(UserSensorPreset preset);
void applyPresetDefaults(UserSensorCfg& cfg);
bool computeCanonical(const UserSensorCfg& cfg, float decoded, float& canon_out);
void formatDisplay(const UserSensorCfg& cfg, bool imperial_units, float canon,
                   float& out_value, const char*& out_unit);

