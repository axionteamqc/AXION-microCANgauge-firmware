#pragma once

#include <stdint.h>

#include "data/datastore.h"

// Internal unit contract for each SignalId.
// All decoders must emit values in these physical units before any UI
// conversion:
// - kMap: kPa (absolute)
// - kClt: deg F
// - kMat: deg F
// - kRpm: RPM
// - kTps: percent (0..100)
// - kAdv / kLaunchTiming / kTcRetard / kKnkRetard: degrees
// - kPw1 / kPw2 / kPwSeq1: milliseconds
// - kEgoCor1: percent
// - kAfr1 / kAfrTarget1: AFR
// - kEgt1: deg F
// - kBatt: volts
// - kVss1: m/s
// - kSensors1 / kSensors2: raw ADC or generic units (no enforced range)
struct SignalContractEntry {
  SignalId id;
  const char* name;
  const char* unit;
  float min;
  float max;
};

const SignalContractEntry* LookupSignalContract(SignalId id);
void ValidateSignalContract(SignalId id, float value);
