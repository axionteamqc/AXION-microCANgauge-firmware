#include <unity.h>
#include "data/datastore.h"

void setUp() {}
void tearDown() {}

void test_future_skew_not_stale() {
  DataStore ds;
  ds.setDefaultStale(500);  // default stale window
  ds.update(SignalId::kMap, 123.0f, 10000, 0);
  const auto r = ds.get(SignalId::kMap, 9990);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FALSE((r.flags & kFlagStale) != 0);
  TEST_ASSERT(r.age_ms <= 5);  // clamped to ~0 with small skew
}

void test_wrap_around_age() {
  DataStore ds;
  ds.setDefaultStale(500);
  ds.update(SignalId::kMap, 1.0f, 0xFFFFFFF0u, 0);
  const auto r = ds.get(SignalId::kMap, 0x00000010u);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_EQUAL_UINT32(0x20u, r.age_ms);  // 32 ms across wrap
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_future_skew_not_stale);
  RUN_TEST(test_wrap_around_age);
  return UNITY_END();
}
