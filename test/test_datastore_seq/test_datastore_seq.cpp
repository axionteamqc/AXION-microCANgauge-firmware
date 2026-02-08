#include <unity.h>
#include "data/datastore.h"

void test_seq_even_after_update() {
  DataStore ds;
  ds.update(SignalId::kMap, 1.23f, 1000, 0);
#ifdef UNIT_TEST
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ds.debug_seq(SignalId::kMap) & 0x1U,
                                   "seq should be even after update");
#endif
}

void test_seq_even_after_invalid() {
  DataStore ds;
  ds.update(SignalId::kMap, 2.0f, 1000, 0);
  ds.note_invalid(SignalId::kMap, 1100, 500);
#ifdef UNIT_TEST
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, ds.debug_seq(SignalId::kMap) & 0x1U,
                                   "seq should be even after note_invalid");
#endif
  auto r = ds.get(SignalId::kMap, 1200);
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_TRUE((r.flags & kFlagInvalid) != 0);
}

void test_get_consistency() {
  DataStore ds;
  ds.update(SignalId::kMap, 5.0f, 100, 0);
  auto r = ds.get(SignalId::kMap, 150);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_EQUAL_FLOAT(5.0f, r.value);
  TEST_ASSERT_TRUE((r.flags & kFlagInvalid) == 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_seq_even_after_update);
  RUN_TEST(test_seq_even_after_invalid);
  RUN_TEST(test_get_consistency);
  return UNITY_END();
}
