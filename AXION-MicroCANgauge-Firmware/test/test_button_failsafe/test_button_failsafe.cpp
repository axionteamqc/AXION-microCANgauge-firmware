#include <unity.h>
#include "config/factory_config.h"

#ifdef UNIT_TEST
bool TestFailsafeShouldRestart(uint32_t pressed_since_ms, uint32_t now_ms,
                               uint8_t* click_count);
#endif

void test_failsafe_not_triggered_before_threshold() {
#ifdef UNIT_TEST
  const uint32_t start = 1000;
  const uint32_t before = start + kFailsafeRestartHoldMs - 10;
  uint8_t click_count = 3;
  TEST_ASSERT_FALSE(TestFailsafeShouldRestart(start, before, &click_count));
  TEST_ASSERT_EQUAL_UINT8(3, click_count);
#endif
}

void test_failsafe_triggers_after_threshold() {
#ifdef UNIT_TEST
  const uint32_t start = 500;
  const uint32_t after = start + kFailsafeRestartHoldMs + 5;
  uint8_t click_count = 3;
  TEST_ASSERT_TRUE(TestFailsafeShouldRestart(start, after, &click_count));
  TEST_ASSERT_EQUAL_UINT8(0, click_count);
#endif
}

void test_failsafe_hold_keeps_clicks_if_short() {
#ifdef UNIT_TEST
  const uint32_t start = 2000;
  const uint32_t before = start + kFailsafeRestartHoldMs - 1;
  uint8_t click_count = 3;
  TEST_ASSERT_FALSE(TestFailsafeShouldRestart(start, before, &click_count));
  TEST_ASSERT_EQUAL_UINT8(3, click_count);
#endif
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_failsafe_not_triggered_before_threshold);
  RUN_TEST(test_failsafe_triggers_after_threshold);
  RUN_TEST(test_failsafe_hold_keeps_clicks_if_short);
  return UNITY_END();
}
