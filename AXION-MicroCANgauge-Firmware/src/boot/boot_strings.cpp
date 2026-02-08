#include "boot/boot_strings.h"

#include <cstring>

#include "app/app_globals.h"
#include "config/factory_config.h"
#include "settings/nvs_store.h"

namespace {
char g_brand[17];
char g_hello1[17];
char g_hello2[17];

void copySafe(char* dst, size_t len, const char* src) {
  if (!dst || len == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strlcpy(dst, src, len);
}
}  // namespace

void BootStringsInitFromNvs() {
  // Defaults from FactoryConfig
  copySafe(g_brand, sizeof(g_brand), kBootBrandText);
  copySafe(g_hello1, sizeof(g_hello1), kBootHelloLine1);
  copySafe(g_hello2, sizeof(g_hello2), kBootHelloLine2);
  String b, h1, h2;
  if (g_nvs.loadBootTexts(b, h1, h2)) {
    if (b.length() > 0) copySafe(g_brand, sizeof(g_brand), b.c_str());
    copySafe(g_hello1, sizeof(g_hello1), h1.c_str());
    copySafe(g_hello2, sizeof(g_hello2), h2.c_str());
  }
}

const char* BootBrandText() { return g_brand; }

const char* BootHello1() { return g_hello1; }

const char* BootHello2() { return g_hello2; }

void BootStringsSet(const char* brand, const char* hello1, const char* hello2) {
  if (brand && brand[0] != '\0') {
    copySafe(g_brand, sizeof(g_brand), brand);
  } else {
    copySafe(g_brand, sizeof(g_brand), kBootBrandText);
  }
  copySafe(g_hello1, sizeof(g_hello1), hello1 ? hello1 : kBootHelloLine1);
  copySafe(g_hello2, sizeof(g_hello2), hello2 ? hello2 : kBootHelloLine2);
}
