#include <unity.h>
#include "settings/nvs_store.h"

void test_factory_reset_clears_wifi_pass() {
  NvsStore store;
  const char* pw = "TestPass123";
  TEST_ASSERT_TRUE(store.saveWifiApPass(pw));
  TEST_ASSERT_TRUE(store.factoryResetClearAll());
  char buf[17] = {0};
  bool ok = store.loadWifiApPass(buf, sizeof(buf));
  TEST_ASSERT_FALSE_MESSAGE(ok, "Wi-Fi pass should be absent after factory reset");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_factory_reset_clears_wifi_pass);
  return UNITY_END();
}
