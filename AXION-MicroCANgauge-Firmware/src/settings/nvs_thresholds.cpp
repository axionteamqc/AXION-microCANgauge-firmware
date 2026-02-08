#include "settings/nvs_store.h"

#include <Preferences.h>

bool NvsStore::loadThresholds(float* mins, float* maxs, size_t count) {
  if (!mins || !maxs) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    String key_min = String(kKeyThrMinPrefix) + String(static_cast<unsigned>(i));
    String key_max = String(kKeyThrMaxPrefix) + String(static_cast<unsigned>(i));
    mins[i] = prefs.getFloat(key_min.c_str(), NAN);
    maxs[i] = prefs.getFloat(key_max.c_str(), NAN);
  }
  prefs.end();
  return true;
}

bool NvsStore::saveThresholds(const float* mins, const float* maxs,
                              size_t count) {
  if (!mins || !maxs) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  bool ok = true;
  for (size_t i = 0; i < count; ++i) {
    String key_min = String(kKeyThrMinPrefix) + String(static_cast<unsigned>(i));
    String key_max = String(kKeyThrMaxPrefix) + String(static_cast<unsigned>(i));
    if (!PutFloatChecked(prefs, key_min.c_str(), mins[i])) ok = false;
    if (!PutFloatChecked(prefs, key_max.c_str(), maxs[i])) ok = false;
  }
  prefs.end();
  return ok;
}
