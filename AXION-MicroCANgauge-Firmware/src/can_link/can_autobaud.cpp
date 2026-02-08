#include "can_link/can_autobaud.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ecu/ecu_profile.h"
#include "config/logging.h"
#include <climits>

namespace {

constexpr size_t kDashSlots = 5;

AutoBaudScore makeScore(const AutoBaudStats& stats) {
  AutoBaudScore s{};
  s.bitrate = stats.bitrate;
  s.rx_total = stats.rx_total;
  uint8_t distinct = 0;
  uint32_t hits = 0;
  for (size_t i = 0; i < kDashSlots; ++i) {
    if (stats.id_counts[i] > 0) {
      ++distinct;
      hits += stats.id_counts[i];
    }
  }
  s.distinct_ids = distinct;
  s.expected_ids_hits = hits;
  s.errors = stats.bus_off + stats.err_passive;
  s.overruns = stats.rx_overrun;
  s.missed = stats.rx_missed;
  // Simple heuristic: reward dash hits and distinct IDs, penalize errors/missed/overrun.
  s.score = static_cast<int32_t>(s.expected_ids_hits) * 100 +
            static_cast<int32_t>(s.distinct_ids) * 200 +
            static_cast<int32_t>(s.rx_total) -
            static_cast<int32_t>(s.errors) * 5000 -
            static_cast<int32_t>(s.overruns) * 2000 -
            static_cast<int32_t>(s.missed) * 10;
  return s;
}

enum class ConfirmResult : uint8_t { kOk, kNoActivity, kFail };

bool meetsIdStability(const AutoBaudStats& stats,
                      const AutobaudSpec& spec) {
  uint8_t ids_seen = 0;
  uint8_t ids_ge3 = 0;
  for (size_t i = 0; i < kDashSlots; ++i) {
    if (stats.id_counts[i] > 0) {
      ++ids_seen;
    }
    if (stats.id_counts[i] >= 3) {
      ++ids_ge3;
    }
  }
  const bool per_id_ok =
      (spec.require_min_per_id == 0) ? false
                                     : (ids_ge3 >= spec.require_min_per_id);
  const bool distinct_ok =
      (spec.require_distinct_ids == 0) ? false : (ids_seen >= spec.require_distinct_ids);
  return per_id_ok || distinct_ok;
}

bool meetsDashMinimum(const AutoBaudStats& stats) {
  const bool has_base = stats.id_counts[0] > 0;
  const bool has_other = (stats.id_present_mask & 0b11110) != 0;
  return has_base && has_other;
}

ConfirmResult confirmListen(const IEcuProfile& profile, TwaiLink& link, uint32_t bitrate,
                            const AutobaudSpec& spec, AutoBaudStats& stats_out) {
  AutoBaudStats stats{};
  stats.bitrate = bitrate;
  const uint32_t window =
        (spec.confirm_window_ms > 0) ? spec.confirm_window_ms
                                   : ((spec.listen_window_ms > 0) ? spec.listen_window_ms : 300);
  const uint8_t windows = (spec.confirm_windows > 0) ? spec.confirm_windows : 1;
  for (uint8_t w = 0; w < windows; ++w) {
    link.stop();
    link.uninstall();
    if (!link.startListenOnly(bitrate)) {
      return ConfirmResult::kFail;
    }
    const uint32_t start_ms = millis();
    while ((millis() - start_ms) < window) {
      twai_message_t msg{};
      while (link.receive(msg, 0)) {
        ++stats.rx_total;
        if (!profile.acceptFrame(msg)) {
          continue;
        }
        const int idx = profile.dashIndexForId(msg.identifier);
        if (idx >= 0 && idx < static_cast<int>(kDashSlots)) {
          ++stats.rx_dash;
          stats.id_present_mask |= static_cast<uint8_t>(1U << idx);
          ++stats.id_counts[idx];
        }
      }
      uint32_t alerts = 0;
      while (link.readAlerts(alerts, 0)) {
        if (alerts & TWAI_ALERT_BUS_OFF) {
          ++stats.bus_off;
        }
        if (alerts & TWAI_ALERT_ERR_PASS) {
          ++stats.err_passive;
        }
        if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
          ++stats.rx_overrun;
        }
      }
      vTaskDelay(1);
    }
    twai_status_info_t status{};
    if (twai_get_status_info(&status) == ESP_OK) {
      stats.rx_missed += status.rx_missed_count;
    }
  }

  stats_out = stats;

  uint8_t distinct_ids = 0;
  uint32_t expected_hits = 0;
  for (size_t i = 0; i < kDashSlots; ++i) {
    if (stats.id_counts[i] > 0) {
      ++distinct_ids;
      expected_hits += stats.id_counts[i];
    }
  }

  const uint16_t min_frames =
      (spec.confirm_min_frames > 0) ? spec.confirm_min_frames : spec.min_rx_total;
  const uint8_t min_distinct =
      (spec.confirm_min_distinct_ids > 0) ? spec.confirm_min_distinct_ids
                                          : spec.require_distinct_ids;
  const uint16_t min_hits =
      (spec.confirm_min_expected_hits > 0) ? spec.confirm_min_expected_hits : spec.min_rx_dash;

  const bool err_ok = (stats.bus_off == 0) && (stats.err_passive == 0) && (stats.rx_overrun == 0);
  if (!err_ok) {
    return ConfirmResult::kFail;
  }
  const bool activity_ok = (stats.rx_total >= min_frames) &&
                           ((min_distinct == 0) ? true : (distinct_ids >= min_distinct)) &&
                           ((min_hits == 0) ? true : (expected_hits >= min_hits)) &&
                           meetsDashMinimum(stats);
  if (!activity_ok) {
    if (stats.rx_total == 0 || stats.rx_dash == 0) {
      return ConfirmResult::kNoActivity;
    }
    return ConfirmResult::kFail;
  }
  return ConfirmResult::kOk;
}

static bool guardNormalWindow(TwaiLink& link, uint32_t bitrate, const AutobaudSpec& spec) {
  const uint32_t guard_window =
      (spec.confirm_window_ms > 0) ? spec.confirm_window_ms : 250;
  if (!link.startNormal(bitrate)) {
    return false;
  }
  AutoBaudStats stats{};
  const uint32_t start_ms = millis();
  while ((millis() - start_ms) < guard_window) {
    twai_message_t msg{};
    while (link.receive(msg, 0)) {
      ++stats.rx_total;
    }
    uint32_t alerts = 0;
    while (link.readAlerts(alerts, 0)) {
      if (alerts & TWAI_ALERT_BUS_OFF) {
        ++stats.bus_off;
      }
      if (alerts & TWAI_ALERT_ERR_PASS) {
        ++stats.err_passive;
      }
      if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
        ++stats.rx_overrun;
      }
    }
    vTaskDelay(1);
  }
  twai_status_info_t status{};
  if (twai_get_status_info(&status) == ESP_OK) {
    stats.rx_missed = status.rx_missed_count;
  }
  link.stop();
  link.uninstall();
  const uint16_t max_missed =
      (spec.max_rx_missed > 0) ? spec.max_rx_missed : 2;
  const bool err_ok =
      (stats.bus_off == 0) && (stats.err_passive == 0) && (stats.rx_overrun == 0) &&
      (stats.rx_missed <= max_missed);
  if (!err_ok) {
    LOGW("Autobaud: NORMAL guard failed, rolling back\r\n");
  }
  return err_ok;
}

static bool validateNormal(const IEcuProfile& profile, TwaiLink& link, uint32_t bitrate,
                           AutoBaudStats& stats_out) {
  const AutobaudSpec& spec = profile.autobaudSpec();
  AutoBaudStats stats{};
  stats.bitrate = bitrate;
  if (!link.startNormal(bitrate)) {
    return false;
  }
  const uint32_t window = (spec.listen_window_ms > 0) ? spec.listen_window_ms : 800;
  const uint32_t start_ms = millis();
  while ((millis() - start_ms) < window) {
    twai_message_t msg{};
    while (link.receive(msg, 0)) {
      ++stats.rx_total;
      if (!profile.acceptFrame(msg)) {
        continue;
      }
      const int idx = profile.dashIndexForId(msg.identifier);
      if (idx >= 0 && idx < static_cast<int>(kDashSlots)) {
        ++stats.rx_dash;
        stats.id_present_mask |= static_cast<uint8_t>(1U << idx);
        ++stats.id_counts[idx];
      }
    }
    uint32_t alerts = 0;
    while (link.readAlerts(alerts, 0)) {
      if (alerts & TWAI_ALERT_BUS_OFF) {
        ++stats.bus_off;
      }
      if (alerts & TWAI_ALERT_ERR_PASS) {
        ++stats.err_passive;
      }
      if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
        ++stats.rx_overrun;
      }
    }
    vTaskDelay(1);
  }
  twai_status_info_t status{};
  if (twai_get_status_info(&status) == ESP_OK) {
    stats.rx_missed = status.rx_missed_count;
  }
  stats_out = stats;
  const uint16_t min_dash = (spec.min_rx_dash > 0) ? spec.min_rx_dash : 12;
  const uint16_t max_missed = (spec.max_rx_missed > 0) ? spec.max_rx_missed : 2;
  const bool id_ok = meetsIdStability(stats, spec) && meetsDashMinimum(stats);
  return (stats.rx_dash >= min_dash) && id_ok && (stats.bus_off == 0) &&
         (stats.err_passive == 0) && (stats.rx_overrun == 0) &&
         (stats.rx_missed <= max_missed);
}

AutoBaudResult RunAutoBaudScanNormalOnly(const IEcuProfile& profile, TwaiLink& link,
                                         AutoBaudStats& result, CanSettings& locked_settings,
                                         AutoBaudDiag& diag) {
  const AutobaudSpec& spec = profile.autobaudSpec();
  uint8_t rate_count = spec.bitrate_count;
  const uint32_t* bitrates = spec.bitrates ? spec.bitrates : profile.scanBitrates(rate_count);

  AutoBaudStats best{};
  AutoBaudStats second{};
  int32_t best_score = INT32_MIN;
  int32_t second_score = INT32_MIN;
  diag.entry_count = 0;
  locked_settings = CanSettings{};

  const uint32_t probe_window =
      (spec.normal_probe_window_ms > 0) ? spec.normal_probe_window_ms : 100;

  auto sampleRate = [&](uint32_t rate) -> AutoBaudStats {
    AutoBaudStats stats{};
    stats.bitrate = rate;
    link.stop();
    link.uninstall();
    if (!link.startNormal(rate)) {
      return stats;
    }
    const uint32_t start_ms = millis();
    while ((millis() - start_ms) < probe_window) {
      twai_message_t msg{};
      while (link.receive(msg, 0)) {
        ++stats.rx_total;
        if (!profile.acceptFrame(msg)) {
          continue;
        }
        const int idx = profile.dashIndexForId(msg.identifier);
        if (idx >= 0 && idx < static_cast<int>(kDashSlots)) {
          ++stats.rx_dash;
          stats.id_present_mask |= static_cast<uint8_t>(1U << idx);
          ++stats.id_counts[idx];
        }
      }
      uint32_t alerts = 0;
      while (link.readAlerts(alerts, 0)) {
        if (alerts & TWAI_ALERT_BUS_OFF) {
          ++stats.bus_off;
        }
        if (alerts & TWAI_ALERT_ERR_PASS) {
          ++stats.err_passive;
        }
        if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
          ++stats.rx_overrun;
        }
      }
      vTaskDelay(1);
    }
    twai_status_info_t status{};
    if (twai_get_status_info(&status) == ESP_OK) {
      stats.rx_missed = status.rx_missed_count;
    }
    link.stop();
    link.uninstall();
    return stats;
  };

  auto considerCandidate = [&](const AutoBaudStats& stats) {
    const AutoBaudScore sc = makeScore(stats);
    LOGV(
        "Autobaud(NORMAL) rate=%lu rx_total=%lu rx_dash=%lu distinct=%u errs=%lu "
        "missed=%lu overrun=%lu score=%ld\r\n",
        static_cast<unsigned long>(stats.bitrate),
        static_cast<unsigned long>(stats.rx_total),
        static_cast<unsigned long>(stats.rx_dash),
        static_cast<unsigned int>(sc.distinct_ids),
        static_cast<unsigned long>(stats.bus_off + stats.err_passive),
        static_cast<unsigned long>(stats.rx_missed),
        static_cast<unsigned long>(stats.rx_overrun),
        static_cast<long>(sc.score));
    if (sc.score > best_score) {
      second_score = best_score;
      second = best;
      best_score = sc.score;
      best = stats;
      if (diag.entry_count < AutoBaudDiag::kMaxEntries) {
        diag.entries[diag.entry_count++] = sc;
      }
    } else if (sc.score > second_score) {
      second_score = sc.score;
      second = stats;
      if (diag.entry_count < AutoBaudDiag::kMaxEntries) {
        diag.entries[diag.entry_count++] = sc;
      }
    }
  };

  for (uint8_t r = 0; r < rate_count; ++r) {
    considerCandidate(sampleRate(bitrates[r]));
  }

  if (best.rx_total == 0 && best.rx_dash == 0) {
    LOGW("Autobaud: NO_CAN_ACTIVITY (normal-only)\r\n");
    diag.reason = AutoBaudResult::kNoActivity;
    result = best;
    return AutoBaudResult::kNoActivity;
  }

  const int32_t clear_delta = 500;
  if (second_score != INT32_MIN && (best_score - second_score) < clear_delta) {
    LOGW("Autobaud: NO_CLEAR_WINNER (normal-only)\r\n");
    diag.reason = AutoBaudResult::kNoClearWinner;
    result = best;
    return AutoBaudResult::kNoClearWinner;
  }

  auto validateCandidate = [&](const AutoBaudStats& candidate) -> bool {
    if (candidate.bitrate == 0 || candidate.rx_total == 0) {
      return false;
    }
    if (!guardNormalWindow(link, candidate.bitrate, spec)) {
      diag.reason = AutoBaudResult::kNormalVerifyFailed;
      return false;
    }
    AutoBaudStats normal_stats{};
    if (validateNormal(profile, link, candidate.bitrate, normal_stats)) {
      locked_settings.bitrate_locked = true;
      locked_settings.bitrate_value = candidate.bitrate;
      locked_settings.id_present_mask = normal_stats.id_present_mask;
      locked_settings.hash_match = true;
      result = candidate;
      result.locked = true;
      return true;
    }
    diag.reason = AutoBaudResult::kNormalVerifyFailed;
    return false;
  };

  if (validateCandidate(best)) {
    diag.reason = AutoBaudResult::kOk;
    return AutoBaudResult::kOk;
  }
  if (second_score != INT32_MIN && (second.rx_total > 0 || second.rx_dash > 0)) {
    if (validateCandidate(second)) {
      diag.reason = AutoBaudResult::kOk;
      return AutoBaudResult::kOk;
    }
  }

  result = best;
  LOGW("Autobaud: NORMAL_VERIFY_FAILED\r\n");
  diag.reason = AutoBaudResult::kNormalVerifyFailed;
  return AutoBaudResult::kNormalVerifyFailed;
}

}  // namespace

AutoBaudResult RunAutoBaudScan(const IEcuProfile& profile, TwaiLink& link, AutoBaudStats& result,
                               CanSettings& locked_settings, AutoBaudDiag* diag_out) {
  LOGV("AutoBaud using profile: %s\n", profile.name());
  const AutobaudSpec& spec = profile.autobaudSpec();
  AutoBaudDiag local_diag{};
  AutoBaudDiag& diag = diag_out ? *diag_out : local_diag;
  if (!spec.allow_listen_only) {
    return RunAutoBaudScanNormalOnly(profile, link, result, locked_settings, diag);
  }

  uint8_t rate_count = spec.bitrate_count;
  const uint32_t* bitrates = spec.bitrates ? spec.bitrates : profile.scanBitrates(rate_count);
  AutoBaudStats best{};
  AutoBaudStats second{};
  int32_t best_score = INT32_MIN;
  int32_t second_score = INT32_MIN;
  locked_settings = CanSettings{};
  diag.entry_count = 0;

  for (uint8_t r = 0; r < rate_count; ++r) {
    const uint32_t rate = bitrates[r];
    link.stop();
    link.uninstall();

    AutoBaudStats stats{};
    stats.bitrate = rate;
    if (!link.startListenOnly(rate)) {
      continue;
    }
      const uint32_t listen_window =
        (spec.listen_window_ms > 0) ? spec.listen_window_ms : 300;
    const uint32_t start_ms = millis();
    while ((millis() - start_ms) < listen_window) {
      twai_message_t msg{};
      while (link.receive(msg, 0)) {
        ++stats.rx_total;
        if (!profile.acceptFrame(msg)) {
          continue;
        }
        const int idx = profile.dashIndexForId(msg.identifier);
        if (idx >= 0 && idx < static_cast<int>(kDashSlots)) {
          ++stats.rx_dash;
          stats.id_present_mask |= static_cast<uint8_t>(1U << idx);
          ++stats.id_counts[idx];
        }
      }
      uint32_t alerts = 0;
      while (link.readAlerts(alerts, 0)) {
        if (alerts & TWAI_ALERT_BUS_OFF) {
          ++stats.bus_off;
        }
        if (alerts & TWAI_ALERT_ERR_PASS) {
          ++stats.err_passive;
        }
        if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
          ++stats.rx_overrun;
        }
      }
      vTaskDelay(1);
    }

    twai_status_info_t status{};
    if (twai_get_status_info(&status) == ESP_OK) {
      stats.rx_missed = status.rx_missed_count;
    }

    const AutoBaudScore sc = makeScore(stats);
    LOGV(
        "Autobaud rate=%lu rx_total=%lu rx_dash=%lu distinct=%u errs=%lu "
        "missed=%lu overrun=%lu score=%ld\r\n",
        static_cast<unsigned long>(rate),
        static_cast<unsigned long>(stats.rx_total),
        static_cast<unsigned long>(stats.rx_dash),
        static_cast<unsigned int>(sc.distinct_ids),
        static_cast<unsigned long>(stats.bus_off + stats.err_passive),
        static_cast<unsigned long>(stats.rx_missed),
        static_cast<unsigned long>(stats.rx_overrun),
        static_cast<long>(sc.score));
    if (sc.score > best_score) {
      second_score = best_score;
      second = best;
      best_score = sc.score;
      best = stats;
      if (diag.entry_count < AutoBaudDiag::kMaxEntries) {
        diag.entries[diag.entry_count++] = sc;
      }
    } else if (sc.score > second_score) {
      second_score = sc.score;
      second = stats;
      if (diag.entry_count < AutoBaudDiag::kMaxEntries) {
        diag.entries[diag.entry_count++] = sc;
      }
    }

    link.stop();
    link.uninstall();
  }

  // Confirm best (and second-best) with an additional listen-only window before NORMAL.
  auto tryCandidate = [&](const AutoBaudStats& candidate) -> bool {
    if (candidate.bitrate == 0 || candidate.rx_total == 0) {
      return false;
    }
    AutoBaudStats confirm_stats{};
    const ConfirmResult cr = confirmListen(profile, link, candidate.bitrate, spec, confirm_stats);
    link.stop();
    link.uninstall();
    if (cr == ConfirmResult::kNoActivity) {
      LOGV("Autobaud confirm: no activity\n");
      return false;
    }
    if (cr != ConfirmResult::kOk) {
      LOGV("Autobaud confirm: unstable\n");
      return false;
    }
    if (!guardNormalWindow(link, candidate.bitrate, spec)) {
      return false;
    }
    AutoBaudStats normal_stats{};
    if (validateNormal(profile, link, candidate.bitrate, normal_stats)) {
      locked_settings.bitrate_locked = true;
      locked_settings.bitrate_value = candidate.bitrate;
      locked_settings.id_present_mask = normal_stats.id_present_mask;
      locked_settings.hash_match = true;
      result = candidate;
      result.locked = true;
      link.stop();
      link.uninstall();
      return true;
    }
    link.stop();
    link.uninstall();
    return false;
  };

  if (best.rx_total == 0 && best.rx_dash == 0) {
    LOGW("Autobaud: NO_CAN_ACTIVITY\r\n");
    diag.reason = AutoBaudResult::kNoActivity;
    result = best;
    return AutoBaudResult::kNoActivity;
  }

  const int32_t clear_delta = 500;
  if (second_score != INT32_MIN && (best_score - second_score) < clear_delta) {
    LOGW("Autobaud: NO_CLEAR_WINNER\r\n");
    diag.reason = AutoBaudResult::kNoClearWinner;
    result = best;
    return AutoBaudResult::kNoClearWinner;
  }

  if (tryCandidate(best)) {
    diag.reason = AutoBaudResult::kOk;
    return AutoBaudResult::kOk;
  }
  if (second_score != INT32_MIN && (second.rx_total > 0 || second.rx_dash > 0)) {
    if (tryCandidate(second)) {
      diag.reason = AutoBaudResult::kOk;
      return AutoBaudResult::kOk;
    }
  }

  result = best;
  LOGW("Autobaud: CONFIRMATION_FAILED\r\n");
  diag.reason = AutoBaudResult::kConfirmFailed;
  return AutoBaudResult::kConfirmFailed;
}
