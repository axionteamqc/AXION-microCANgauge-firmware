#include <unity.h>
#include "settings/nvs_store.h"

void test_save_and_load_wifi_pass() {
  NvsStore store;
  // Use a short pass within 16 chars.
  const char* pass = "TestPass123";
  TEST_ASSERT_TRUE(store.saveWifiApPass(pass));
  char buf[17] = {0};
  TEST_ASSERT_TRUE(store.loadWifiApPass(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING(pass, buf);
}

void test_clear_wifi_pass() {
  NvsStore store;
  const char* pass = "TmpPass456";
  TEST_ASSERT_TRUE(store.saveWifiApPass(pass));
  TEST_ASSERT_TRUE(store.clearWifiApPass());
  char buf[17] = {0};
  TEST_ASSERT_FALSE(store.loadWifiApPass(buf, sizeof(buf)));
}

void test_reject_long_pass() {
  NvsStore store;
  const char* long_pass = "12345678901234567890";  // >16
  TEST_ASSERT_FALSE(store.saveWifiApPass(long_pass));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_save_and_load_wifi_pass);
  RUN_TEST(test_clear_wifi_pass);
  RUN_TEST(test_reject_long_pass);
  return UNITY_END();
}
