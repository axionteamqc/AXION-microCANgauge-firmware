#include <unity.h>
#include "wifi/wifi_pass_validate.h"

void test_pass_valid() {
  TEST_ASSERT_TRUE(ValidateWifiApPass("Abcdef12"));
}

void test_pass_reject_lt_8() {
  TEST_ASSERT_FALSE(ValidateWifiApPass("short"));
}

void test_pass_reject_gt_16() {
  TEST_ASSERT_FALSE(ValidateWifiApPass("12345678901234567"));  // 17 chars
}

void test_pass_reject_bad_char() {
  TEST_ASSERT_FALSE(ValidateWifiApPass("<badpass1"));
}

void test_pass_reject_non_printable() {
  const char bad[] = {'A', 'B', 'C', '\t', '1', '2', '3', 0};
  TEST_ASSERT_FALSE(ValidateWifiApPass(bad));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pass_valid);
  RUN_TEST(test_pass_reject_lt_8);
  RUN_TEST(test_pass_reject_gt_16);
  RUN_TEST(test_pass_reject_bad_char);
  RUN_TEST(test_pass_reject_non_printable);
  return UNITY_END();
}
