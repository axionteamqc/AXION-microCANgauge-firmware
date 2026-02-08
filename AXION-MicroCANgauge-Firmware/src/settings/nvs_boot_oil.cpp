#include "settings/nvs_store.h"

#include <Preferences.h>
#include <cstring>

#include "config/factory_config.h"
#include "config/logging.h"

bool NvsStore::loadOilPersist(OilPersist& out) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  out.mode = prefs.getUChar("oil_mode", out.mode);
  out.swap = prefs.getBool("oil_swap", out.swap);
  String lp = prefs.getString("oil_lblp", out.label_p);
  String lt = prefs.getString("oil_lblt", out.label_t);
  strlcpy(out.label_p, lp.c_str(), sizeof(out.label_p));
  strlcpy(out.label_t, lt.c_str(), sizeof(out.label_t));
  prefs.end();
  return true;
}

bool NvsStore::saveOilPersist(const OilPersist& in) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  prefs.putUChar("oil_mode", in.mode);
  prefs.putBool("oil_swap", in.swap);
  prefs.putString("oil_lblp", in.label_p);
  prefs.putString("oil_lblt", in.label_t);
  prefs.end();
  return true;
}

bool NvsStore::loadBootTexts(String& brand, String& h1, String& h2) {
  if (!ready_) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  // brand default always applies if key missing or empty
  brand = prefs.getString(kKeyBootBrand, kBootBrandText);
  if (brand.length() == 0) brand = kBootBrandText;

  // hello1/hello2 default only if key missing
  if (prefs.isKey(kKeyBootHello1)) {
    h1 = prefs.getString(kKeyBootHello1, "");
  } else {
    h1 = kBootHelloLine1;
  }
  if (prefs.isKey(kKeyBootHello2)) {
    h2 = prefs.getString(kKeyBootHello2, "");
  } else {
    h2 = kBootHelloLine2;
  }
  prefs.end();
  return true;
}

bool NvsStore::saveBootTexts(const String& brand, const String& h1, const String& h2) {
  if (!ready_) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }
  // brand: empty -> remove to fallback default
  bool ok = true;
  if (brand.length() == 0) {
    ok &= prefs.remove(kKeyBootBrand);
#if NVS_VERIFY_WRITES
    if (ok && prefs.isKey(kKeyBootBrand)) {
      LOGE("NVS verify failed: %s\r\n", kKeyBootBrand);
      ok = false;
    }
#endif
  } else {
    ok &= PutStringChecked(prefs, kKeyBootBrand, brand.c_str());
  }
  // hello1/hello2: keep even if empty (empty = disable)
  ok &= PutStringChecked(prefs, kKeyBootHello1, h1.c_str());
  ok &= PutStringChecked(prefs, kKeyBootHello2, h2.c_str());
  prefs.end();
  return ok;
}

bool NvsStore::loadCfgPending(bool& pending) {
  pending = false;
  if (!ready_) return false;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }
  pending = prefs.getBool(kKeyCfgPending, false);
  prefs.end();
  return true;
}

bool NvsStore::setCfgPending(bool pending) {
  if (!ready_) {
    LOGE("NVS setCfgPending failed: store not ready\r\n");
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    LOGE("NVS setCfgPending failed: prefs.begin\r\n");
    return false;
  }
  const size_t written = prefs.putBool(kKeyCfgPending, pending);
  prefs.end();
  if (written == 0) {
    LOGE("NVS setCfgPending failed: write\r\n");
    return false;
  }
  return true;
}
