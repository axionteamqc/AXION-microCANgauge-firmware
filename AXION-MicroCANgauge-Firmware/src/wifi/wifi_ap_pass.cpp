#include "wifi/wifi_ap_pass.h"

#include <cstring>

#include "app/app_globals.h"

namespace {

constexpr const char* kSsid = "AXION-MCG";
constexpr const char* kDefaultPass = "AMCG1234";
static char s_ap_pass[17] = "AMCG1234";  // 16 chars max + null
static bool s_ap_pass_loaded = false;

static bool IsPrintableAscii(const char* s) {
  if (!s) return false;
  for (const char* p = s; *p; ++p) {
    if (*p < 32 || *p > 126) return false;
  }
  return true;
}

static bool IsValidPass(const char* pass) {
  if (!pass) return false;
  const size_t len = strlen(pass);
  if (len < 8 || len > 16) return false;
  return IsPrintableAscii(pass);
}

static void LoadApPassFromNvsOnce() {
  if (s_ap_pass_loaded) return;
  s_ap_pass_loaded = true;
  char buf[sizeof(s_ap_pass)] = {0};
  if (g_nvs.loadWifiApPass(buf, sizeof(buf))) {
    if (IsValidPass(buf)) {
      strlcpy(s_ap_pass, buf, sizeof(s_ap_pass));
      return;
    }
  }
  strlcpy(s_ap_pass, kDefaultPass, sizeof(s_ap_pass));
  // Optionally repair NVS if it contained an invalid value.
  g_nvs.saveWifiApPass(kDefaultPass);
}

}  // namespace

const char* WifiApSsid() { return kSsid; }

const char* WifiApPass() {
  LoadApPassFromNvsOnce();
  return s_ap_pass;
}

bool WifiApPassSetAndPersist(const char* new_pass) {
  if (!IsValidPass(new_pass)) return false;
  if (!g_nvs.saveWifiApPass(new_pass)) return false;
  strlcpy(s_ap_pass, new_pass, sizeof(s_ap_pass));
  s_ap_pass_loaded = true;
  return true;
}

void WifiApPassResetToDefault() {
  LoadApPassFromNvsOnce();
  strlcpy(s_ap_pass, kDefaultPass, sizeof(s_ap_pass));
  s_ap_pass_loaded = true;
  g_nvs.saveWifiApPass(kDefaultPass);
}

bool WifiApPassValid(const char* pass) { return IsValidPass(pass); }
