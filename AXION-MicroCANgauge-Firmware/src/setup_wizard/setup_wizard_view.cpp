#include "app_config.h"
#if SETUP_WIZARD_ENABLED
// SetupWizard rendering: focused screen text output.
#include "setup_wizard/setup_wizard.h"

#include <cstring>

namespace {
OledU8g2& pickDisplay(const AppState& state, OledU8g2& primary,
                      OledU8g2& secondary, uint8_t focus) {
  if (state.dual_screens) {
    if (focus == 1 && state.oled_secondary_ready) return secondary;
    return primary;
  }
  return state.oled_primary_ready ? primary : secondary;
}
}  // namespace

void SetupWizard::renderFocused(const AppState& state, OledU8g2& disp,
                                uint32_t now_ms) {
  (void)now_ms;
  char l1[18] = {0};
  char l2[18] = {0};
  char l3[18] = {0};
  char l4[18] = {0};

  if (debug_view_) {
    snprintf(l1, sizeof(l1), "RAW CAN PROOF");
    snprintf(l2, sizeof(l2), "ID %03lX DLC%u",
             static_cast<unsigned long>(debug_last_id_ & 0x7FFu),
             static_cast<unsigned>(debug_last_dlc_));
    snprintf(l3, sizeof(l3), "RX=%lu FPS=%lu",
             static_cast<unsigned long>(debug_rx_total_),
             static_cast<unsigned long>(debug_fps_));
    disp.drawLines(l1, l2, l3, nullptr);
    return;
  }

  switch (phase_) {
    case Phase::kKoeoIntro:
      snprintf(l1, sizeof(l1), "SETUP KOEO");
      break;
    case Phase::kKoeoScanMenu:
      snprintf(l1, sizeof(l1), "CAN SETUP");
      snprintf(l2, sizeof(l2), "DBL=AUTO SCAN");
      snprintf(l3, sizeof(l3), "SRT=MANUAL");
      snprintf(l4, sizeof(l4), "SCAN MAY PERTURB");
      break;
    case Phase::kKoeoScanRun:
      snprintf(l1, sizeof(l1), "SCANNING...");
      if (last_diag_.entry_count > 0) {
        const AutoBaudScore& e0 = last_diag_.entries[0];
        snprintf(l2, sizeof(l2), "RATE %lu rx=%lu",
                 static_cast<unsigned long>(e0.bitrate / 1000),
                 static_cast<unsigned long>(e0.rx_total));
      }
      break;
    case Phase::kKoeoScanLocked:
      snprintf(l1, sizeof(l1), "FOUND %luk",
               static_cast<unsigned long>(locked_rate_ / 1000));
      snprintf(l2, sizeof(l2), "DBL=SAVE+REBOOT");
      snprintf(l3, sizeof(l3), "SRT=BACK");
      break;
    case Phase::kKoeoScanSaved:
      snprintf(l1, sizeof(l1), "SAVED, REBOOT");
      break;
    case Phase::kKoeoValidateBitrate:
      snprintf(l1, sizeof(l1), "VALIDATE RATE");
      snprintf(l2, sizeof(l2), "RX=%lu DASH=%lu",
               static_cast<unsigned long>(validate_rx_total_),
               static_cast<unsigned long>(validate_rx_dash_));
      break;
    case Phase::kKoeoManualBitrate:
      snprintf(l1, sizeof(l1), "MANUAL RATE");
      snprintf(l2, sizeof(l2), "%luk NEXT/PREV",
               static_cast<unsigned long>(validate_rate_ / 1000));
      snprintf(l3, sizeof(l3), "DBL=START");
      break;
    case Phase::kKoeoMeasurePeriod:
      snprintf(l1, sizeof(l1), "MEASURING");
      snprintf(l2, sizeof(l2), "IDs:%u", static_cast<unsigned>(state.id_present_mask));
      break;
    case Phase::kKoeoBaro:
      snprintf(l1, sizeof(l1), "BARO");
      snprintf(l2, sizeof(l2), "MAP %.1fkPa", last_map_kpa_);
      break;
    case Phase::kKoeoValidate:
      snprintf(l1, sizeof(l1), "VALIDATE");
      break;
    case Phase::kKoeoDone:
      snprintf(l1, sizeof(l1), "KOEO OK");
      break;
    case Phase::kRunIntro:
      snprintf(l1, sizeof(l1), "SETUP RUN");
      break;
    case Phase::kRunCapture:
      snprintf(l1, sizeof(l1), "BLIP RPM");
      snprintf(l2, sizeof(l2), "RPM %.0f", last_rpm_);
      snprintf(l3, sizeof(l3), "MAP %.1f", last_map_kpa_);
      break;
    case Phase::kRunValidate:
      snprintf(l1, sizeof(l1), "VALIDATE");
      break;
  case Phase::kSetupDone:
    snprintf(l1, sizeof(l1), "RUN OK");
    break;
  case Phase::kKoeoFail:
    if (last_diag_.entry_count > 0) {
      const AutoBaudScore& e0 = last_diag_.entries[0];
      const char* reason = "UNKNOWN";
      switch (fail_reason_) {
        case FailReason::kNoFrames:
          reason = "NO FRAMES";
          break;
        case FailReason::kCanStartFail:
          reason = "CAN FAIL";
          break;
        case FailReason::kLowScore:
          reason = "LOW SCORE";
          break;
        case FailReason::kBusError:
          reason = "BUS ERROR";
          break;
        case FailReason::kProfileMismatch:
          reason = "PROFILE MISM";
          break;
        default:
          break;
      }
      snprintf(l1, sizeof(l1), "SCAN FAIL");
      snprintf(l2, sizeof(l2), "%luk rx=%lu ids=%u",
               static_cast<unsigned long>(e0.bitrate / 1000),
               static_cast<unsigned long>(e0.rx_total),
               static_cast<unsigned int>(e0.distinct_ids));
      snprintf(l3, sizeof(l3), "hits=%lu err=%lu",
               static_cast<unsigned long>(e0.expected_ids_hits),
               static_cast<unsigned long>(e0.errors));
      snprintf(l4, sizeof(l4), "%s", reason);
    } else {
      snprintf(l1, sizeof(l1), "SCAN FAIL");
      snprintf(l2, sizeof(l2), "SRT=AUTO SCAN");
      snprintf(l3, sizeof(l3), "DBL=MANUAL");
    }
    break;
  case Phase::kRunFail:
    snprintf(l1, sizeof(l1), "RUN FAIL");
    snprintf(l2, sizeof(l2), "SRT=RETRY");
    break;
    case Phase::kInactive:
    default:
      break;
  }

  disp.drawLines(l1[0] ? l1 : nullptr, l2[0] ? l2 : nullptr,
                 l3[0] ? l3 : nullptr, l4[0] ? l4 : nullptr);
}

void SetupWizard::render(const AppState& state, OledU8g2& oled_primary,
                         OledU8g2& oled_secondary, uint32_t now_ms) {
  if (!isActive()) return;
  if (!state.oled_primary_ready && !state.oled_secondary_ready) return;
  OledU8g2& disp =
      pickDisplay(state, oled_primary, oled_secondary, focus_screen_);
  renderFocused(state, disp, now_ms);
}

#endif  // SETUP_WIZARD_ENABLED
