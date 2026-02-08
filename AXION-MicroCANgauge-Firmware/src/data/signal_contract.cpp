#include "data/signal_contract.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "config/logging.h"
#endif

const SignalContractEntry* LookupSignalContract(SignalId id) {
  static const SignalContractEntry kTable[] = {
      {SignalId::kMap, "MAP", "kPa", 10.0f, 400.0f},
      {SignalId::kClt, "CLT", "F", -40.0f, 300.0f},
      {SignalId::kMat, "MAT", "F", -40.0f, 260.0f},
      {SignalId::kRpm, "RPM", "rpm", 0.0f, 12000.0f},
      {SignalId::kTps, "TPS", "%", 0.0f, 100.0f},
      {SignalId::kAdv, "ADV", "deg", -40.0f, 80.0f},
      {SignalId::kLaunchTiming, "Launch", "deg", -40.0f, 80.0f},
      {SignalId::kTcRetard, "TC Retard", "deg", 0.0f, 30.0f},
      {SignalId::kPw1, "PW1", "ms", 0.0f, 50.0f},
      {SignalId::kPw2, "PW2", "ms", 0.0f, 50.0f},
      {SignalId::kPwSeq1, "PWSeq1", "ms", 0.0f, 50.0f},
      {SignalId::kEgoCor1, "EGOcor1", "%", 0.0f, 200.0f},
      {SignalId::kAfr1, "AFR1", "AFR", 5.0f, 25.0f},
      {SignalId::kAfrTarget1, "AFRtg1", "AFR", 5.0f, 25.0f},
      {SignalId::kEgt1, "EGT1", "F", 200.0f, 2000.0f},
      {SignalId::kBatt, "BATT", "V", 8.0f, 18.0f},
      {SignalId::kKnkRetard, "Knk", "deg", 0.0f, 20.0f},
      {SignalId::kVss1, "VSS1", "m/s", 0.0f, 120.0f},
      {SignalId::kSensors1, "SENS1", "raw", -1e6f, 1e6f},
      {SignalId::kSensors2, "SENS2", "raw", -1e6f, 1e6f},
  };
  for (const auto& e : kTable) {
    if (e.id == id) return &e;
  }
  return nullptr;
}

void ValidateSignalContract(SignalId id, float value) {
#ifdef DEBUG_VALIDATE_SIGNAL_CONTRACT
  const SignalContractEntry* ent = LookupSignalContract(id);
  if (!ent) return;
  if (value < ent->min || value > ent->max) {
#ifdef ARDUINO
    if (kEnableVerboseSerialLogs) {
      LOGW(
          "[SIGNAL] out-of-range %s (expected %s): %.3f (min %.3f max %.3f)\r\n",
          ent->name, ent->unit, static_cast<double>(value),
          static_cast<double>(ent->min), static_cast<double>(ent->max));
    }
#else
    (void)ent;
    (void)value;
#endif
  }
#else
  (void)id;
  (void)value;
#endif
}
