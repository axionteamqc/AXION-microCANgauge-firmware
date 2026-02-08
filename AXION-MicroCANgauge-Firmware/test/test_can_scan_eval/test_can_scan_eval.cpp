#include <unity.h>
#include "app/can_recovery_eval.h"

void test_can_scan_ok_nominal() {
  CanScanEvalIn in{};
  in.started_ok = true;
  in.normal_mode = true;
  in.rx_dash = 6;
  in.min_dash = 5;
  in.bus_off = 0;
  in.err_passive = 0;
  in.rx_overrun = 0;
  in.rx_missed = 0;
  TEST_ASSERT_TRUE(CanScanEvalOk(in, 2));
}

void test_can_scan_ko_bus_off() {
  CanScanEvalIn in{};
  in.started_ok = true;
  in.normal_mode = true;
  in.rx_dash = 6;
  in.min_dash = 5;
  in.bus_off = 1;
  TEST_ASSERT_FALSE(CanScanEvalOk(in, 2));
}

void test_can_scan_ko_missed() {
  CanScanEvalIn in{};
  in.started_ok = true;
  in.normal_mode = true;
  in.rx_dash = 6;
  in.min_dash = 5;
  in.rx_missed = 3;
  TEST_ASSERT_FALSE(CanScanEvalOk(in, 2));
}

void test_can_scan_ko_rx_dash() {
  CanScanEvalIn in{};
  in.started_ok = true;
  in.normal_mode = true;
  in.rx_dash = 4;
  in.min_dash = 5;
  TEST_ASSERT_FALSE(CanScanEvalOk(in, 2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_can_scan_ok_nominal);
  RUN_TEST(test_can_scan_ko_bus_off);
  RUN_TEST(test_can_scan_ko_missed);
  RUN_TEST(test_can_scan_ko_rx_dash);
  return UNITY_END();
}
