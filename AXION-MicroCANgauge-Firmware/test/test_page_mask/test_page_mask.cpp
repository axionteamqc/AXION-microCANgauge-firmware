#include <unity.h>

#include "app/page_mask.h"

void test_page_mask_all_basic() {
  TEST_ASSERT_EQUAL_UINT32(0u, PageMaskAll(0));
  TEST_ASSERT_EQUAL_UINT32(0x1u, PageMaskAll(1));
  TEST_ASSERT_EQUAL_UINT32(0x7FFFFFFFu, PageMaskAll(31));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, PageMaskAll(32));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, PageMaskAll(33));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_page_mask_all_basic);
  return UNITY_END();
}
