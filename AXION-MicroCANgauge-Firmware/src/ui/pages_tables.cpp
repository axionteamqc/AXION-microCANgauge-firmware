#include "ui/pages.h"

namespace {

const PageMeta kPageMeta[] = {
    {PageId::kOilP, "OILP", ValueKind::kPressure, true, false},
    {PageId::kOilT, "OILT", ValueKind::kTemp, false, true},
    {PageId::kBoost, "BOOST", ValueKind::kPressure, true, true},
    {PageId::kMapAbs, "MAP", ValueKind::kPressure, true, true},
    {PageId::kRpm, "RPM", ValueKind::kRpm, true, true},
    {PageId::kClt, "CLT", ValueKind::kTemp, true, true},
    {PageId::kMat, "IAT", ValueKind::kTemp, true, true},
    {PageId::kBatt, "BATT", ValueKind::kVoltage, true, true},
    {PageId::kTps, "TPS", ValueKind::kPercent, true, true},
    {PageId::kAdv, "ADV", ValueKind::kDeg, true, true},
    {PageId::kAfr1, "AFR", ValueKind::kAfr, true, true},
    {PageId::kAfrTgt, "AFR TG", ValueKind::kAfr, true, true},
    {PageId::kKnk, "KNK", ValueKind::kDeg, false, true},
    {PageId::kVss, "VSS", ValueKind::kSpeed, true, true},
    {PageId::kEgt1, "EGT", ValueKind::kTemp, true, true},
    {PageId::kPw1, "PW1", ValueKind::kNone, false, true},
    {PageId::kPw2, "PW2", ValueKind::kNone, false, true},
    {PageId::kPwSeq, "PWSEQ", ValueKind::kNone, false, true},
    {PageId::kEgo, "EGO", ValueKind::kPercent, true, true},
    {PageId::kLaunch, "LCH", ValueKind::kDeg, true, true},
    {PageId::kTc, "TC", ValueKind::kDeg, true, true}};

const PageDef kPages[] = {
    {PageId::kOilP, "OILP"},   {PageId::kOilT, "OILT"},
    {PageId::kBoost, "BOOST"}, {PageId::kMapAbs, "MAP"},
    {PageId::kRpm, "RPM"},     {PageId::kClt, "CLT"},
    {PageId::kMat, "IAT"},     {PageId::kBatt, "BATT"},
    {PageId::kTps, "TPS"},     {PageId::kAdv, "ADV"},
    {PageId::kAfr1, "AFR"},    {PageId::kAfrTgt, "AFR TG"},
    {PageId::kKnk, "KNK"},     {PageId::kVss, "VSS"},
    {PageId::kEgt1, "EGT"},    {PageId::kPw1, "PW1"},
    {PageId::kPw2, "PW2"},     {PageId::kPwSeq, "PWSEQ"},
    {PageId::kEgo, "EGO"},     {PageId::kLaunch, "LCH"},
    {PageId::kTc, "TC"}};

}  // namespace

const PageDef* GetPageTable(size_t& count) {
  count = sizeof(kPages) / sizeof(kPages[0]);
  return kPages;
}

const PageMeta* GetPageMeta(size_t& count) {
  count = sizeof(kPageMeta) / sizeof(kPageMeta[0]);
  return kPageMeta;
}

const PageMeta* FindPageMeta(PageId id) {
  for (size_t i = 0; i < sizeof(kPageMeta) / sizeof(kPageMeta[0]); ++i) {
    if (kPageMeta[i].id == id) {
      return &kPageMeta[i];
    }
  }
  return nullptr;
}
