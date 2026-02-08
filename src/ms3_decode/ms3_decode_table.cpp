#include "ms3_decode/ms3_decode_table.h"

namespace {

// 1512 (0x5E8)
constexpr Ms3SignalSpec kMsg0Signals[] = {
    {SignalId::kTps, 55, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kClt, 39, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kRpm, 23, 16, false, 1.0f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kMap, 7, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
};

// 1513 (0x5E9)
constexpr Ms3SignalSpec kMsg1Signals[] = {
    {SignalId::kAdv, 55, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kMat, 39, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kPw2, 23, 16, false, 0.001f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kPw1, 7, 16, false, 0.001f, 0.0f, BitOrder::MotorolaDBC},
};

// 1514 (0x5EA)
constexpr Ms3SignalSpec kMsg2Signals[] = {
    {SignalId::kPwSeq1, 55, 16, true, 0.001f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kEgt1, 39, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kEgoCor1, 23, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kAfr1, 15, 8, false, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kAfrTarget1, 7, 8, false, 0.1f, 0.0f, BitOrder::MotorolaDBC},
};

// 1515 (0x5EB)
constexpr Ms3SignalSpec kMsg3Signals[] = {
    {SignalId::kKnkRetard, 55, 8, false, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kSensors2, 39, 16, true, 0.01f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kSensors1, 23, 16, true, 0.01f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kBatt, 7, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
};

// 1516 (0x5EC)
constexpr Ms3SignalSpec kMsg4Signals[] = {
    {SignalId::kLaunchTiming, 39, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kTcRetard, 23, 16, true, 0.1f, 0.0f, BitOrder::MotorolaDBC},
    {SignalId::kVss1, 7, 16, false, 0.1f, 0.0f, BitOrder::MotorolaDBC},
};

constexpr Ms3MessageSpec kMsgs[] = {
    {0x5E8, kMsg0Signals, static_cast<uint8_t>(sizeof(kMsg0Signals) /
                                               sizeof(kMsg0Signals[0]))},
    {0x5E9, kMsg1Signals, static_cast<uint8_t>(sizeof(kMsg1Signals) /
                                               sizeof(kMsg1Signals[0]))},
    {0x5EA, kMsg2Signals, static_cast<uint8_t>(sizeof(kMsg2Signals) /
                                               sizeof(kMsg2Signals[0]))},
    {0x5EB, kMsg3Signals, static_cast<uint8_t>(sizeof(kMsg3Signals) /
                                               sizeof(kMsg3Signals[0]))},
    {0x5EC, kMsg4Signals, static_cast<uint8_t>(sizeof(kMsg4Signals) /
                                               sizeof(kMsg4Signals[0]))},
};

}  // namespace

const Ms3MessageSpec kMs3Messages[] = {
    kMsgs[0], kMsgs[1], kMsgs[2], kMsgs[3], kMsgs[4],
};

const size_t kMs3MessageCount = sizeof(kMsgs) / sizeof(kMsgs[0]);
