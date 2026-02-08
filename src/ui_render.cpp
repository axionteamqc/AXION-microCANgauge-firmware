#include "ui_render.h"

#include <Arduino.h>
#include <cstring>

#include "app_config.h"
#include "config/logging.h"
#include "wifi/wifi_portal.h"

// g_state ownership summary:
// - CAN RX task writes CAN stats/state under g_state_mux.
// - UI loop writes UI/navigation fields; UI-only fields may be read without lock.
// - Wi-Fi portal applies config and writes shared fields under g_state_mux.
// Rule: any field read across tasks must be written under g_state_mux.

#ifdef DEBUG_STALE_OLED2
#include "data/datastore.h"
#endif

namespace {

bool logicalScreenReady(const AppState& state, uint8_t screen_index) {
  const PhysicalDisplayId disp = ZoneToDisplay(state, screen_index);
  const bool primary_ok = state.oled_primary_ready;
  const bool secondary_ok = state.oled_secondary_ready;
  switch (disp) {
    case PhysicalDisplayId::kPrimary:
      return primary_ok || (!state.dual_screens && secondary_ok);
    case PhysicalDisplayId::kSecondary:
      return secondary_ok || (!state.dual_screens && primary_ok);
    default:
      return false;
  }
}

OledU8g2& screenDisplay(AppState& state, OledU8g2& oled_primary,
                        OledU8g2& oled_secondary, uint8_t screen_index) {
  const PhysicalDisplayId disp = ZoneToDisplay(state, screen_index);
  switch (disp) {
    case PhysicalDisplayId::kPrimary:
      return state.oled_primary_ready ? oled_primary : oled_secondary;
    case PhysicalDisplayId::kSecondary:
      return state.oled_secondary_ready ? oled_secondary : oled_primary;
    default:
      return oled_primary;
  }
}

#ifdef DEBUG_STALE_OLED2
bool PageToSignal(PageId id, SignalId& out) {
  switch (id) {
    case PageId::kMapAbs:
    case PageId::kBoost:
      out = SignalId::kMap;
      return true;
    case PageId::kClt:
      out = SignalId::kClt;
      return true;
    case PageId::kMat:
      out = SignalId::kMat;
      return true;
    case PageId::kRpm:
      out = SignalId::kRpm;
      return true;
    case PageId::kTps:
      out = SignalId::kTps;
      return true;
    case PageId::kBatt:
      out = SignalId::kBatt;
      return true;
    case PageId::kAdv:
      out = SignalId::kAdv;
      return true;
    case PageId::kAfr1:
      out = SignalId::kAfr1;
      return true;
    case PageId::kAfrTgt:
      out = SignalId::kAfrTarget1;
      return true;
    case PageId::kKnk:
      out = SignalId::kKnkRetard;
      return true;
    case PageId::kVss:
      out = SignalId::kVss1;
      return true;
    case PageId::kEgt1:
      out = SignalId::kEgt1;
      return true;
    case PageId::kPw1:
      out = SignalId::kPw1;
      return true;
    case PageId::kPw2:
      out = SignalId::kPw2;
      return true;
    case PageId::kPwSeq:
      out = SignalId::kPwSeq1;
      return true;
    case PageId::kEgo:
      out = SignalId::kEgoCor1;
      return true;
    case PageId::kLaunch:
      out = SignalId::kLaunchTiming;
      return true;
    case PageId::kTc:
      out = SignalId::kTcRetard;
      return true;
    default:
      return false;
  }
}
#endif

static const char* CanOverlayReason(const AppState& s, uint32_t now_ms) {
  const uint32_t rx_ok = s.can_stats.rx_ok_count;
  const uint32_t rx_errs = s.can_stats.rx_err_count +
                           s.can_stats.rx_drop_count +
                           s.can_stats.bus_off +
                           s.can_stats.err_passive +
                           s.can_stats.rx_overrun;
  if (!s.can_ready) {
    if (s.last_bus_off_ms != 0 && (now_ms - s.last_bus_off_ms) < 60000U) {
      return "BUS OFF";
    }
    return "CAN DOWN";
  }
  if (s.can_safe_listen) return "SAFE LISTEN";
  const bool no_rx = rx_ok == 0;
  const bool has_err = rx_errs > 0;
  if (s.can_link.health == CanHealth::kNoFrames && s.can_edge_active &&
      s.can_edge_rate > 200.0f) {
    return "BUS ACTIVE/BIT?";
  }
  if (no_rx && has_err) {
    return s.can_bitrate_locked ? "BAD BUS" : "BAD BIT";
  }
  if (no_rx) {
    return "NO FRAMES";
  }
  if (s.can_link.state == CanLinkState::kNoProfileMatch &&
      s.can_link.health != CanHealth::kDecodeBad) {
    return "NO MATCH";
  }
  switch (s.can_link.health) {
    case CanHealth::kNoFrames:
      return "NO FRAMES";
    case CanHealth::kDecodeBad:
      if (!s.can_bitrate_locked) return "BAD BIT";
      if (s.can_link.state == CanLinkState::kNoProfileMatch) return "BAD ECU";
      return "BAD CFG";
    case CanHealth::kImplausible:
      return "DECODE ERR";
    case CanHealth::kStale:
      return "STALE";
    case CanHealth::kOk:
    default:
      return "CAN ISSUE";
  }
}

}  // namespace

void resetMaxForFocusPage(AppState& state, uint32_t now_ms) {
  const uint8_t focus = state.dual_screens ? state.focus_screen : 0;
  const uint8_t page = state.page_index[focus] % kPageCount;
  state.max_blink_until_ms[focus] = now_ms + 700;
  state.max_blink_page[focus] = page;
}

void renderScreen(AppState& state, OledU8g2& oled_primary,
                  OledU8g2& oled_secondary, const DataStore& store,
                  const AlertsEngine& alerts, uint8_t screen_index,
                  uint32_t now_ms, bool allow_refresh, uint8_t viewport_y = 0,
                  uint8_t viewport_h = 0, bool clear_buffer = true,
                  bool send_buffer = true) {
  if (!logicalScreenReady(state, screen_index)) {
    return;
  }
  size_t page_count = 0;
  const PageDef* pages = GetPageTable(page_count);
  const uint8_t page = state.page_index[screen_index] % page_count;
  const PageDef& def = pages[page];
  const PageMeta* meta = FindPageMeta(def.id);
  const bool page_units = GetPageUnits(state, page);
  ScreenSettings display_cfg =
      DisplayConfigForZone(state, static_cast<uint8_t>(screen_index));
  display_cfg.imperial_units = page_units;
  const PhysicalDisplayId disp =
      ZoneToDisplay(state, static_cast<uint8_t>(screen_index));
  OledU8g2& disp_obj =
      screenDisplay(state, oled_primary, oled_secondary, screen_index);

  const bool editing =
      state.edit_mode.mode[screen_index] != EditModeState::Mode::kNone &&
      state.edit_mode.page[screen_index] == page;
  const AlertLevel level = alerts.alertForPage(def.id);
  const bool alert_active = level != AlertLevel::kNone;
  const bool in_menu =
      state.ui_menu.isActive() && screen_index == state.focus_screen;
  const bool invert_on =
      alert_active && !editing && !in_menu && !state.wizard_active;
  const bool topo_large =
      state.display_topology == DisplayTopology::kLargeOnly ||
      state.display_topology == DisplayTopology::kLargePlusSmall;
  const bool lock_toast =
      state.lock_toast_until_ms[screen_index] > now_ms &&
      state.lock_toast_type[screen_index] != AppState::LockToast::kNone;

  RenderState& rs = state.render_state[screen_index];
  const bool shared_viewport = viewport_h > 0 &&
                               disp_obj.height() > viewport_h;
  const bool allow_global_invert = !topo_large && !shared_viewport;
  if (allow_global_invert) {
    if (invert_on != rs.last_invert) {
      disp_obj.setInvert(invert_on);
      rs.last_invert = invert_on;
    } else if (invert_on || rs.last_invert) {
      // Keep inversion stable even if not redrawing.
      disp_obj.setInvert(invert_on);
    }
  } else {
    // In large split topologies, keep hardware invert off for this zone.
    if (rs.last_invert) {
      disp_obj.setInvert(false);
      rs.last_invert = false;
    }
  }

  PageRenderData data = BuildPageData(def.id, state, display_cfg, store, now_ms);
#ifdef DEBUG_STALE_OLED2
  static bool was_stale[kMaxZones] = {false, false, false};
  const bool is_stale = data.has_error && (strcmp(data.err_a, "STAL") == 0);
  if (is_stale && !was_stale[screen_index]) {
    SignalId sig;
    if (PageToSignal(def.id, sig)) {
#if CORE_DEBUG_LEVEL >= 3
      if (kEnableVerboseSerialLogs) {
        const SignalRead r = store.get(sig, now_ms);
        LOGI(
            "[STALE] start t=%lu scr=%u page=%u sig=%u valid=%d age=%lu flags=0x%02X val=%.3f last_can=%lu\n",
            static_cast<unsigned long>(now_ms),
            static_cast<unsigned>(screen_index),
            static_cast<unsigned>(def.id), static_cast<unsigned>(sig), r.valid,
            static_cast<unsigned long>(r.age_ms), r.flags,
            static_cast<double>(r.value),
            static_cast<unsigned long>(state.last_can_rx_ms));
      }
#endif
    } else {
#if CORE_DEBUG_LEVEL >= 3
      if (kEnableVerboseSerialLogs) {
        LOGI(
            "[STALE] start t=%lu scr=%u page=%u (no sig map) last_can=%lu\n",
            static_cast<unsigned long>(now_ms),
            static_cast<unsigned>(screen_index),
            static_cast<unsigned>(def.id),
            static_cast<unsigned long>(state.last_can_rx_ms));
      }
#endif
    }
  } else if (!is_stale && was_stale[screen_index]) {
#if CORE_DEBUG_LEVEL >= 3
    if (kEnableVerboseSerialLogs) {
      LOGI("[STALE] clear scr=%u page=%u t=%lu\n",
           static_cast<unsigned>(screen_index),
           static_cast<unsigned>(def.id),
           static_cast<unsigned long>(now_ms));
    }
#endif
  }
  was_stale[screen_index] = is_stale;
#endif
  const bool heartbeat = (rs.last_draw_ms == 0) ||
                         (now_ms - rs.last_draw_ms) >= 200;
  if (!allow_refresh && !heartbeat && !state.force_redraw[screen_index]) {
    return;
  }
  const bool can_bus_error =
      AppConfig::IsRealCanEnabled() && !state.demo_mode &&
      ((!state.can_ready) || state.can_safe_listen ||
       state.can_link.state == CanLinkState::kNoProfileMatch ||
       state.can_link.health == CanHealth::kNoFrames ||
       state.can_link.health == CanHealth::kDecodeBad ||
       state.can_link.health == CanHealth::kImplausible ||
       state.can_link.health == CanHealth::kStale);
  if (can_bus_error && !in_menu) {
    const char* reason = CanOverlayReason(state, now_ms);
    if (clear_buffer || !shared_viewport) {
      disp_obj.simpleClear();
    }
    disp_obj.simpleSetFontSmall();
    const uint8_t y1 = static_cast<uint8_t>(viewport_y + 10);
    const uint8_t y2 = static_cast<uint8_t>(y1 + 12);
    disp_obj.simpleDrawStr(0, y1, "CAN ERROR");
    disp_obj.simpleDrawStr(0, y2, reason);
    if (send_buffer) {
      disp_obj.simpleSend();
      state.force_redraw[screen_index] = false;
      state.last_oled_ms[screen_index] = now_ms;
      rs.last_draw_ms = now_ms;
    }
    return;
  }
  if (state.ui_menu.isActive() && screen_index == state.focus_screen) {
    state.ui_menu.render(
        screenDisplay(state, oled_primary, oled_secondary, screen_index),
        DisplayConfigForZone(state, static_cast<uint8_t>(screen_index)),
        meta ? meta->kind : ValueKind::kNone, page_units,
        GetPageMaxAlertEnabled(state, page),
        GetPageMinAlertEnabled(state, page), state.display_topology, disp,
        false, def.label, static_cast<uint8_t>(page), clear_buffer, viewport_y,
        viewport_h, send_buffer);
    if (send_buffer) {
      state.force_redraw[screen_index] = false;
      state.last_oled_ms[screen_index] = now_ms;
      rs.last_draw_ms = now_ms;
    }
    return;
  }

  float sample_value = 0.0f;
  if (data.valid && data.has_canon && !editing) {
    sample_value = data.canon_value;
  }

  char big_buf[16];
  char suffix_buf[8];
  char max_buf[16];
  if (data.valid) {
    strlcpy(big_buf, data.big, sizeof(big_buf));
  } else if (data.has_error) {
    strlcpy(big_buf, data.err_a, sizeof(big_buf));
  } else {
    strlcpy(big_buf, "---", sizeof(big_buf));
  }
  if (data.valid) {
    strlcpy(suffix_buf, data.suffix, sizeof(suffix_buf));
  } else if (data.has_error) {
    strlcpy(suffix_buf, data.err_b, sizeof(suffix_buf));
  } else {
    strlcpy(suffix_buf, "", sizeof(suffix_buf));
  }
  max_buf[0] = '\0';
  if (editing && meta) {
    // Show editable threshold; use display units stored in edit_mode.
    float disp_val = state.edit_mode.display_value[screen_index];
    ThresholdGrid grid{};
    if (GetThresholdGrid(def.id, display_cfg.imperial_units, grid)) {
      const char* fmt = (grid.decimals == 0) ? "%.0f" : "%.1f";
      snprintf(big_buf, sizeof(big_buf), fmt, static_cast<double>(disp_val));
    } else {
      snprintf(big_buf, sizeof(big_buf), "%.1f", static_cast<double>(disp_val));
    }
    switch (meta->kind) {
      case ValueKind::kPressure:
      case ValueKind::kBoost:
        data.unit = display_cfg.imperial_units ? "psi" : "kPa";
        break;
      case ValueKind::kTemp:
        data.unit = display_cfg.imperial_units ? "F" : "C";
        break;
      case ValueKind::kVoltage:
        data.unit = "V";
        break;
      case ValueKind::kPercent:
        data.unit = "%";
        break;
      case ValueKind::kSpeed:
        data.unit = display_cfg.imperial_units ? "mph" : "km/h";
        break;
    case ValueKind::kRpm:
      data.unit = "rpm";
      break;
    case ValueKind::kDeg:
      data.unit = "deg";
      break;
    case ValueKind::kAfr:
      data.unit = "AFR";
      break;
    default:
      break;
  }
    data.valid = true;
    const bool blink_on = ((now_ms / 250U) % 2U) == 0U;
    const char* tag = (state.edit_mode.mode[screen_index] ==
                       EditModeState::Mode::kEditMax)
                          ? "MAX"
                          : "MIN";
    if (blink_on) {
      strlcpy(max_buf, tag, sizeof(max_buf));
    } else {
      max_buf[0] = '\0';
    }
  }
  // Extrema view overrides big/unit with recorded maxima/minima.
  if (state.extrema_view.active[screen_index] &&
      state.extrema_view.page[screen_index] == page && meta) {
    const bool show_min = state.extrema_view.show_min[screen_index];
    const float canon = show_min ? state.page_recorded_min[page]
                                 : state.page_recorded_max[page];
    if (!isnan(canon)) {
      const float disp_val = CanonToDisplay(meta->kind, canon, display_cfg);
      switch (meta->kind) {
        case ValueKind::kVoltage:
        case ValueKind::kAfr:
        case ValueKind::kPercent:
        case ValueKind::kPressure:
        case ValueKind::kBoost:
          snprintf(big_buf, sizeof(big_buf), "%.1f",
                   static_cast<double>(disp_val));
          break;
        case ValueKind::kTemp:
        case ValueKind::kSpeed:
        case ValueKind::kDeg:
        case ValueKind::kRpm:
          snprintf(big_buf, sizeof(big_buf), "%.0f",
                   static_cast<double>(disp_val));
          break;
        default:
          snprintf(big_buf, sizeof(big_buf), "%.1f",
                   static_cast<double>(disp_val));
          break;
      }
      strlcpy(max_buf, show_min ? "RMIN" : "RMAX", sizeof(max_buf));
      data.valid = true;
    } else {
      strlcpy(big_buf, "---", sizeof(big_buf));
      max_buf[0] = '\0';
      data.valid = false;
    }
  }
  const char* unit_str = data.unit;

  const uint8_t edit_phase = editing ? static_cast<uint8_t>((now_ms / 250U) & 0x1U) : 0;
  const bool max_blink_active =
      now_ms < state.max_blink_until_ms[screen_index] &&
      page == state.max_blink_page[screen_index];
  const uint8_t maxblink_phase =
      max_blink_active ? static_cast<uint8_t>((now_ms / 120U) & 0x1U) : 0;
  const bool exclam_blink_on = ((now_ms / 250U) % 2U) == 0U;
  const bool changed =
      state.force_redraw[screen_index] || (rs.last_page != page) ||
      (rs.last_value != sample_value) ||
      (rs.last_valid != data.valid) ||
      (editing && (edit_phase != rs.last_blink_phase)) ||
      (max_blink_active && (maxblink_phase != rs.last_maxblink_phase)) ||
      ((screen_index == state.focus_screen) != rs.last_focused) ||
      ((level != AlertLevel::kNone) && (exclam_blink_on != rs.last_exclam_phase)) ||
      (lock_toast != rs.last_lock_toast);
  const uint32_t kMinRenderIntervalMs = 120;
  const bool allow_interval = (rs.last_draw_ms == 0) ||
                              (now_ms - rs.last_draw_ms) >= kMinRenderIntervalMs;
  if ((changed && (state.force_redraw[screen_index] || allow_interval)) ||
      heartbeat) {
    const AlertLevel level = alerts.alertForPage(def.id);
    const bool crit = alerts.hasCritical();
    const bool warn_marker = (level == AlertLevel::kWarn) && exclam_blink_on;
    const bool crit_marker = (level == AlertLevel::kCrit) && exclam_blink_on;
    // Blink units opposite to the alert icon: show units only when no icon blink.
    const bool hide_units = (level != AlertLevel::kNone) && exclam_blink_on;
    const char* unit_for_render = hide_units ? "" : unit_str;
    const char* value_for_render = nullptr;
    const char* suffix_for_render = "";
    if (data.valid) {
      value_for_render = big_buf;
      suffix_for_render = suffix_buf;
    } else if (data.has_error) {
      if (strcmp(data.err_a, "INV") != 0) {
        value_for_render = big_buf;
        suffix_for_render = suffix_buf;
      }
    }
    disp_obj.renderMetric(def.label, value_for_render, suffix_for_render, unit_for_render, max_buf,
                          data.valid, screen_index == state.focus_screen,
                          warn_marker, crit_marker,
                          crit, viewport_y, viewport_h, clear_buffer, send_buffer,
                          shared_viewport ? invert_on : false,
                          topo_large ? invert_on : false);
    if (lock_toast) {
      disp_obj.simpleSetFontSmall();
      const char* msg = "LOCK";
      switch (state.lock_toast_type[screen_index]) {
        case AppState::LockToast::kUnlocked:
          msg = "UNLOCK";
          break;
        case AppState::LockToast::kBlocked:
          msg = "LOCKED";
          break;
        case AppState::LockToast::kLocked:
        case AppState::LockToast::kNone:
        default:
          msg = "LOCK";
          break;
      }
      disp_obj.simpleDrawStr(0, static_cast<uint8_t>(viewport_y + 12), msg);
    }
    rs.last_page = page;
    rs.last_value = sample_value;
    rs.last_valid = data.valid;
    rs.last_draw_ms = now_ms;
    rs.last_blink_phase = edit_phase;
    rs.last_maxblink_phase = maxblink_phase;
    rs.last_focused = (screen_index == state.focus_screen);
    rs.last_exclam_phase = exclam_blink_on;
    rs.last_lock_toast = lock_toast;
    state.force_redraw[screen_index] = false;
    state.last_oled_ms[screen_index] = now_ms;
  }
}

void renderUi(AppState& state, const DataStore& store,
              OledU8g2& oled_primary, OledU8g2& oled_secondary,
              uint32_t now_ms, bool allow_oled1, bool allow_oled2,
              const AlertsEngine& alerts) {
  static bool s_was_wifi = false;
  if (state.sleep) {
    return;
  }

  if (state.wifi_mode_active) {
    if (state.oled_primary_ready) {
      oled_primary.setSleep(false);
      const uint32_t phase = (now_ms / 2000U) % 5U;
      const char* line1 = "WIFI MODE";
      const char* line2 = nullptr;
      if (state.wifi_exit_confirm) {
        line2 = "LONG=CONFIRM";
      } else {
        switch (phase) {
          case 0:
            line2 = "SSID: AXION-MCG";
            break;
          case 1:
            static char pw_line[24];
            snprintf(pw_line, sizeof(pw_line), "MDP: %s", WifiPortalPass());
            line2 = pw_line;
            break;
          case 2:
            line2 = "URL: axion.local";
            break;
          case 3:
            line2 = "IP: 192.168.4.1";
            break;
          default:
            line2 = "LONG=EXIT";
            break;
        }
      }
      if (allow_oled1) {
        oled_primary.drawLines(line1, line2, nullptr, nullptr);
      }
    }
    if (state.oled_secondary_ready && !s_was_wifi) {
      // Force black frame then sleep once on entry to avoid ghost content.
      oled_secondary.simpleClear();
      oled_secondary.simpleSend();
      oled_secondary.setSleep(true);
    }
    s_was_wifi = true;
    return;
  }

  if (s_was_wifi) {
    if (state.oled_secondary_ready) {
      oled_secondary.setSleep(false);
    }
    for (uint8_t z = 0; z < kMaxZones; ++z) {
      state.force_redraw[z] = true;
    }
    s_was_wifi = false;
  }

  // Boot-safe: if topology is unconfigured, render only the top 32px of OLED1.
  if (state.display_topology == DisplayTopology::kUnconfigured) {
    if (allow_oled1 && state.oled_primary_ready) {
      oled_primary.simpleClear();
      oled_primary.simpleSetFontSmall();
      oled_primary.simpleDrawStr(0, 6, "SELECT DISPLAY MODE");
      const char* lines[4] = {"1xSmall", "2xSmall", "S+L", "1xLarge"};
      for (uint8_t i = 0; i < 4; ++i) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%c %s",
                 (state.display_setup_index == i) ? '>' : ' ', lines[i]);
        const int16_t y = 14 + static_cast<int16_t>(i) * 8;
        if (y < 32) oled_primary.simpleDrawStr(0, y, buf);
      }
      oled_primary.simpleSend();
    }
    return;
  }

  const DisplayTopology topo = state.display_topology;

  if ((topo == DisplayTopology::kLargeOnly ||
       topo == DisplayTopology::kLargePlusSmall) &&
      state.oled_primary_ready) {
    static uint32_t last_large_log_ms = 0;
    if ((now_ms - last_large_log_ms) >= 1000U) {
#if CORE_DEBUG_LEVEL >= 3
      if (kEnableVerboseSerialLogs) {
        LOGI(
            "render large: top=z0 page=%u bottom=z1 page=%u small=z2 page=%u\r\n",
            static_cast<unsigned int>(state.page_index[0] % kPageCount),
            static_cast<unsigned int>(state.page_index[1] % kPageCount),
            static_cast<unsigned int>(state.page_index[2] % kPageCount));
      }
#endif
      last_large_log_ms = now_ms;
    }
    // Render zones 0/1 on primary (128x64), send once.
    renderScreen(state, oled_primary, oled_secondary, store, alerts, 0, now_ms,
                 allow_oled1, 0, 32, true, false);
    renderScreen(state, oled_primary, oled_secondary, store, alerts, 1, now_ms,
                 allow_oled1, 32, 32, false, true);
    if (topo == DisplayTopology::kLargePlusSmall && state.oled_secondary_ready) {
      renderScreen(state, oled_primary, oled_secondary, store, alerts, 2, now_ms,
                   allow_oled2);
    }
    return;
  }

  if (topo == DisplayTopology::kDualSmall) {
    if (state.oled_primary_ready) {
      renderScreen(state, oled_primary, oled_secondary, store, alerts, 0,
                   now_ms, allow_oled1);
    }
    if (state.oled_secondary_ready) {
      renderScreen(state, oled_primary, oled_secondary, store, alerts, 2,
                   now_ms, allow_oled2);
    }
    return;
  }

  // Small-only or fallback: render zone 0 on whichever display is ready.
  if (state.oled_primary_ready) {
    const bool restrict_top =
        topo == DisplayTopology::kSmallOnly && oled_primary.height() > 32;
    renderScreen(state, oled_primary, oled_secondary, store, alerts, 0, now_ms,
                 allow_oled1, 0, restrict_top ? 32 : 0, true, true);
  } else if (state.oled_secondary_ready) {
    renderScreen(state, oled_primary, oled_secondary, store, alerts, 0, now_ms,
                 allow_oled2);
  }
}
