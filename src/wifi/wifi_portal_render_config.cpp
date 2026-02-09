#include "wifi/wifi_portal_render_config.h"

#include <cmath>
#include <cstdio>

#include "app/app_sleep.h"
#include "boot/boot_strings.h"
#include "config/factory_config.h"
#include "wifi/wifi_portal_escape.h"
#include "wifi/wifi_portal_units.h"

namespace {

struct UnitOption {
  bool selectable;
  const char* metric;
  const char* imperial;
};

UnitOption unitOptionsForMeta(const PageMeta* meta) {
  if (!meta) return {false, "", ""};
  switch (meta->kind) {
    case ValueKind::kPressure:
    case ValueKind::kBoost:
      return {true, "kPa", "psi"};
    case ValueKind::kTemp:
      return {true, "C", "F"};
    case ValueKind::kSpeed:
      return {true, "km/h", "mph"};
    case ValueKind::kVoltage:
      return {false, "V", "V"};
    case ValueKind::kAfr:
      return {false, "AFR", "AFR"};
    case ValueKind::kPercent:
      return {false, "%", "%"};
    case ValueKind::kDeg:
      return {false, "deg", "deg"};
    case ValueKind::kRpm:
      return {false, "rpm", "rpm"};
    case ValueKind::kNone: {
      switch (meta->id) {
        case PageId::kPw1:
        case PageId::kPw2:
        case PageId::kPwSeq:
          return {false, "ms", "ms"};
        default:
          return {false, "", ""};
      }
    }
    default:
      return {false, "", ""};
  }
}

const char* topoToStr(DisplayTopology t) {
  switch (t) {
    case DisplayTopology::kSmallOnly:
      return "1xSmall";
    case DisplayTopology::kDualSmall:
      return "2xSmall";
    case DisplayTopology::kLargeOnly:
      return "1xLarge";
    case DisplayTopology::kLargePlusSmall:
      return "S+L";
    case DisplayTopology::kUnconfigured:
    default:
      return "Unconfigured";
  }
}

void renderPerPageRow(const SendFn& send, size_t index, const PageDef* pages,
                      const PageMeta* meta, const AppUiSnapshot& ui) {
  const bool imperial =
      (ui.page_units_mask & (1U << static_cast<uint8_t>(index))) != 0;
  const ValueKind kind = meta ? meta->kind : ValueKind::kNone;
  ScreenSettings cfg{};
  cfg.imperial_units = imperial;
  cfg.flip_180 = false;
  const bool amax =
      (ui.page_alert_max_mask & (1U << static_cast<uint8_t>(index))) != 0;
  const bool amin =
      (ui.page_alert_min_mask & (1U << static_cast<uint8_t>(index))) != 0;
  const bool hidden =
      (ui.page_hidden_mask & (1U << static_cast<uint8_t>(index))) != 0;
  const UnitOption uo = unitOptionsForMeta(meta);
  // Stream output to avoid large per-row String allocations in portal render.
  char idx_buf[16];
  snprintf(idx_buf, sizeof(idx_buf), "%u", static_cast<unsigned>(index));
  send("<tr><td>");
  send(idx_buf);
  send("</td><td>");
  send((meta && meta->label) ? meta->label : "PAGE");
  send("</td><td>");
  if (uo.selectable) {
    send("<select name='units_");
    send(idx_buf);
    send("'>");
    send("<option value='0'");
    if (!imperial) send(" selected");
    send(">");
    send(uo.metric);
    send("</option>");
    send("<option value='1'");
    if (imperial) send(" selected");
    send(">");
    send(uo.imperial);
    send("</option>");
    send("</select>");
  } else {
    send(uo.metric);
    send("<input type='hidden' name='units_");
    send(idx_buf);
    send("' value='");
    send(imperial ? "1" : "0");
    send("'>");
  }
  send("</td>");
  ThresholdGrid grid{};
  const bool has_grid = GetThresholdGrid(pages[index].id, imperial, grid);
  auto fmtVal = [&](float v, char* out, size_t out_len) -> const char* {
    const int decimals = static_cast<int>(grid.decimals);
    snprintf(out, out_len, "%.*f", decimals, static_cast<double>(v));
    return out;
  };
  auto currentToDisplay = [&](float canon) -> float {
    if (isnan(canon)) return NAN;
    float disp = CanonToDisplay(kind, canon, cfg);
    return has_grid ? SnapToGrid(disp, grid) : disp;
  };
  float disp_min = currentToDisplay(ui.thresholds[index].min);
  float disp_max = currentToDisplay(ui.thresholds[index].max);
  auto appendSelect = [&](const char* name, float current_disp) {
    send("<select name='");
    send(name);
    send("'>");
    send("<option value=''");
    if (isnan(current_disp)) send(" selected");
    send(">NA</option>");
    if (has_grid && grid.step_disp > 0.0f) {
      char val_buf[32];
      for (float v = grid.min_disp; v <= grid.max_disp + grid.step_disp * 0.0001f;
           v += grid.step_disp) {
        send("<option value='");
        send(fmtVal(v, val_buf, sizeof(val_buf)));
        send("'");
        if (!isnan(current_disp)) {
          const float eps = grid.step_disp * 0.001f;
          if (fabsf(current_disp - v) < eps) send(" selected");
        }
        send(">");
        send(fmtVal(v, val_buf, sizeof(val_buf)));
        send("</option>");
      }
    }
    send("</select>");
  };
  send("<td>");
  char name_buf[32];
  snprintf(name_buf, sizeof(name_buf), "thr_min_%u",
           static_cast<unsigned>(index));
  appendSelect(name_buf, disp_min);
  send("</td><td>");
  snprintf(name_buf, sizeof(name_buf), "thr_max_%u",
           static_cast<unsigned>(index));
  appendSelect(name_buf, disp_max);
  send("</td><td><input type='checkbox' name='amax_");
  send(idx_buf);
  send("'");
  if (amax) send(" checked");
  send("></td><td><input type='checkbox' name='amin_");
  send(idx_buf);
  send("'");
  if (amin) send(" checked");
  send("></td><td><input type='checkbox' name='hide_");
  send(idx_buf);
  send("'");
  if (hidden) send(" checked");
  send("></td></tr>");
}

}  // namespace

void renderDownloads(const SendFn& send) {
  send("<h2>Downloads</h2><ul>");
  send("<li><a href='/download/report.csv'>report.csv</a></li>");
  send("<li><a href='/download/config.json'>config.json</a></li>");
  send("</ul>");
}

void renderSetupSnapshot(const SendFn& send, size_t page_count,
                         const AppUiSnapshot& ui) {
  send("<h2>Setup</h2><ul>");
  send("<li>FW: ");
  send(kFirmwareVersion);
  send(" | Build: ");
  send(kBuildId);
  send("</li>");
  send("<li>Display: ");
  send(topoToStr(static_cast<DisplayTopology>(ui.display_topology)));
  send("</li>");
  send("<li>Screen 1 flip: ");
  send(ui.screen_flip[0] ? "ON" : "OFF");
  send(" &nbsp; Screen 2 flip: ");
  send(ui.screen_flip[1] ? "ON" : "OFF");
  send("</li>");
  send("<li>Boot pages: [");
  char num_buf[32];
  for (uint8_t z = 0; z < kMaxZones; ++z) {
    if (z > 0) send(", ");
    snprintf(num_buf, sizeof(num_buf), "%u",
             static_cast<unsigned>(ui.boot_page_index[z]));
    send(num_buf);
  }
  send("]</li>");
  send("<li>Pages: ");
  snprintf(num_buf, sizeof(num_buf), "%u", static_cast<unsigned>(page_count));
  send(num_buf);
  send("</li>");
  send("<li>BARO: ");
  if (ui.baro_acquired) {
    snprintf(num_buf, sizeof(num_buf), "%.1f",
             static_cast<double>(ui.baro_kpa));
    send(num_buf);
    send(" kPa (OK)");
  } else {
    send("(not set) - KOEO then wait");
  }
  send("</li>");
  send("</ul>");
}

void renderBootSelect(const SendFn& send, size_t page_count, uint8_t current,
                      const char* name, const char* label, const char* sel_id,
                      const char* hidden_id) {
  char num_buf[32];
  send("<div class='boot-row'>");
  send("<label for='");
  send(sel_id);
  send("'>");
  send(label);
  send("</label>");
  send("<select name='");
  send(name);
  send("_sel' id='");
  send(sel_id);
  send("' onchange='syncBootPages()'>");
  const PageDef* pages = GetPageTable(page_count);
  for (size_t i = 0; i < page_count; ++i) {
    const PageMeta* meta = FindPageMeta(pages[i].id);
    snprintf(num_buf, sizeof(num_buf), "%u", static_cast<unsigned>(i));
    send("<option value='");
    send(num_buf);
    send("'");
    if (current == i) send(" selected");
    send(">");
    if (meta && meta->label) {
      send(meta->label);
    } else {
      send("PAGE ");
      send(num_buf);
    }
    send("</option>");
  }
  send("</select>");
  send("<input type='hidden' name='");
  send(hidden_id);
  send("' id='");
  send(hidden_id);
  send("' value='");
  snprintf(num_buf, sizeof(num_buf), "%u", static_cast<unsigned>(current));
  send(num_buf);
  send("'>");
  send("</div>");
}

void renderPerPageTable(const SendFn& send, size_t page_count,
                        const PageDef* pages, const AppUiSnapshot& ui) {
  send("<h3>Per-page Settings</h3>");
  send("<p><button type='button' onclick='setAllAlerts(true)'>Check all</button>"
       " <button type='button' onclick='setAllAlerts(false)'>Uncheck all</button></p>");
  send("<div class='table-wrap'>");
  send("<table class='wide'><tr>"
       "<th>#</th>"
       "<th>Page</th>"
       "<th>Units</th>"
       "<th>Thr min</th>"
       "<th>Thr max</th>"
       "<th>Alert Max</th>"
       "<th>Alert Min</th>"
       "<th>Hide</th>"
       "</tr>");
  for (size_t i = 0; i < page_count; ++i) {
    if ((i % 3) == 0) AppSleepMs(0);
    const PageMeta* meta = FindPageMeta(pages[i].id);
    renderPerPageRow(send, i, pages, meta, ui);
  }
  send("</table>");
  send("</div>");
}

void renderBootTextInputs(const SendFn& send) {
  send.SendRaw("<div style='margin-top:10px;'>Boot brand text: "
               "<input name='boot_brand_text' maxlength='16' value='");
  SendHtmlEscaped(send, BootBrandText());
  send.SendRaw("'></div>");
  send.SendRaw("<div style='margin-top:6px;'>Hello line1 text: "
               "<input name='hello_line1_text' maxlength='16' value='");
  SendHtmlEscaped(send, BootHello1());
  send.SendRaw("'></div>");
  send.SendRaw("<div style='margin-top:6px; margin-bottom:10px;'>Hello line2 text: "
               "<input name='hello_line2_text' maxlength='16' value='");
  SendHtmlEscaped(send, BootHello2());
  send.SendRaw("'></div>");
}

void renderActions(const SendFn& send) {
  send("<h2>Actions</h2>");
  send("<p><label><input type='checkbox' id='confirm_all' name='confirm' value='1'> Confirm</label></p>");
  send("<div class='actions'>");
  send("<button type='submit' name='action' value='apply' "
       "onclick=\"syncBootPages(); return submitAction('Apply config and reboot?');\">Config & Reboot</button>");
  send("<button type='button' onclick=\"location.href='/fw';\">Firmware Update</button>");
  send("<button type='button' onclick=\"location.href='/i2c';\">I2C/OLED Logs</button>");
  send("<button type='submit' name='action' value='reset_extrema' "
       "onclick=\"syncBootPages(); return submitAction('Reset recorded extrema and max values?');\">Reset Extrema</button>");
  send("<button type='submit' name='action' value='factory_reset' "
       "onclick=\"syncBootPages(); return submitAction('Factory reset and reboot?');\">Factory Reset</button>");
  send("<button type='submit' name='action' value='reset_baro' "
       "onclick=\"return submitAction('Recalibrate BARO and reboot?');\">Recalibrate BARO</button>");
  send("</div>");
}

void renderRecordedExtrema(const SendFn& send, size_t page_count,
                           const PageDef* pages, const AppUiSnapshot& ui) {
  send("<h2>Recorded Extrema</h2>");
  send("<div class='table-wrap'>");
  send("<table class='wide'><tr><th>#</th><th>Page</th><th>Units</th><th>Rec min</th><th>Rec max</th>"
       "<th>Thr min</th><th>Thr max</th><th>Alert max</th><th>Alert min</th></tr>");
  for (size_t i = 0; i < page_count; ++i) {
    if ((i % 3) == 0) AppSleepMs(0);
    const PageMeta* meta = FindPageMeta(pages[i].id);
    const ValueKind kind = meta ? meta->kind : ValueKind::kNone;
    ScreenSettings cfg{};
    cfg.imperial_units =
        (ui.page_units_mask & (1U << static_cast<uint8_t>(i))) != 0;
    cfg.flip_180 = false;
    const char* units = unitsForKind(kind, cfg.imperial_units);
    const float rec_min = ui.page_recorded_min[i];
    const float rec_max = ui.page_recorded_max[i];
    const float thr_min = ui.thresholds[i].min;
    const float thr_max = ui.thresholds[i].max;
    const auto fmtVal = [&](float v, char* out, size_t out_len) -> const char* {
      if (isnan(v)) {
        snprintf(out, out_len, "NA");
        return out;
      }
      const float disp = CanonToDisplay(kind, v, cfg);
      int decimals = 0;
      if (kind == ValueKind::kPressure || kind == ValueKind::kBoost ||
          kind == ValueKind::kVoltage || kind == ValueKind::kAfr ||
          kind == ValueKind::kPercent) {
        decimals = 1;
      }
      snprintf(out, out_len, "%.*f", decimals, static_cast<double>(disp));
      return out;
    };
    const bool alert_max =
        (ui.page_alert_max_mask & (1U << static_cast<uint8_t>(i))) != 0;
    const bool alert_min =
        (ui.page_alert_min_mask & (1U << static_cast<uint8_t>(i))) != 0;
    char buf[24];
    char idx_buf[16];
    snprintf(idx_buf, sizeof(idx_buf), "%u", static_cast<unsigned>(i));
    send("<tr><td>");
    send(idx_buf);
    send("</td><td>");
    send((meta && meta->label) ? meta->label : "PAGE");
    send("</td><td>");
    send(units);
    send("</td><td>");
    send(fmtVal(rec_min, buf, sizeof(buf)));
    send("</td><td>");
    send(fmtVal(rec_max, buf, sizeof(buf)));
    send("</td><td>");
    send(fmtVal(thr_min, buf, sizeof(buf)));
    send("</td><td>");
    send(fmtVal(thr_max, buf, sizeof(buf)));
    send("</td><td>");
    send(alert_max ? "ON" : "OFF");
    send("</td><td>");
    send(alert_min ? "ON" : "OFF");
    send("</td></tr>");
  }
  send("</table>");
  send("</div>");
}
