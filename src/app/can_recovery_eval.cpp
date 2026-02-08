#include "can_recovery_eval.h"

bool CanScanEvalOk(const CanScanEvalIn& in, uint32_t max_missed) {
  if (!in.started_ok || !in.normal_mode) {
    return false;
  }
  if (in.rx_dash < in.min_dash) {
    return false;
  }
  if (in.bus_off != 0 || in.err_passive != 0 || in.rx_overrun != 0) {
    return false;
  }
  if (in.rx_missed > max_missed) {
    return false;
  }
  return true;
}
