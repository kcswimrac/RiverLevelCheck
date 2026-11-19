// River forecast screen for the 450x600 portrait AMOLED.
//
//   SAN JOAQUIN RIVER                       battery
//   VERNALIS · VNSC1
//   ─────────────────────────────
//        NOW        │   EXPECTED PEAK
//       8.66        │      10.42           <- 72 px digits (fit up to 39.99)
//    ft · 7:45 AM   │  ft · Sat 4:00 AM
//   NEXT 5 DAYS                     <- amber when the forecast is short/gappy/old
//   [ chart: 24 h observed (grey) | NOW | forecast to its last hour (colour) ]
//   Updated Tue 11/17 9:00 AM PST
//   ( COMPLETE 5-DAY FORECAST )     <- status pill
#include "RiverUi.h"

#include <math.h>
#include <stdio.h>

#include "src/core/pacific_time.h"
#include "src/fonts/fonts.h"

namespace {

constexpr uint32_t COL_BG = 0x000000, COL_TEXT = 0xF2F4F7, COL_MUTED = 0x8A94A6, COL_DIM = 0x2A2F38,
                   COL_PANEL = 0x0E1218, COL_ACCENT = 0x38BDF8, COL_OK = 0x34D399, COL_WARN = 0xF5A524,
                   COL_BAD = 0xF2555A, COL_DEMO = 0xA78BFA, COL_HIST = 0xB8C0CC;
// River-level bands for the chart: green below 20 ft, yellow 20-28 ft, red above 28 ft.
constexpr uint32_t COL_LVL_OK = 0x34D399, COL_LVL_WATCH = 0xFACC15, COL_LVL_HIGH = 0xF2555A;
constexpr int LEVEL_WATCH_CFT = 2000, LEVEL_HIGH_CFT = 2800;   // hundredths of a foot
// EXPECTED PEAK turns yellow above 28 ft, or when it is this far above the current reading.
constexpr float PEAK_RISE_ALERT_FT = 1.0f;
constexpr int W = 450;
constexpr int REFRESH_SHOW_S = 120;

uint16_t c565(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}

uint16_t mix(uint32_t a, uint32_t b, float t) {   // t=0 -> a, t=1 -> b
  auto ch = [&](int sh) { return (uint32_t)(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t) << sh; };
  return c565(ch(16) | ch(8) | ch(0));
}

uint32_t level_color(int cft) {
  return cft < LEVEL_WATCH_CFT ? COL_LVL_OK : cft <= LEVEL_HIGH_CFT ? COL_LVL_WATCH : COL_LVL_HIGH;
}

struct Ctx {
  TFT_eSprite& s;
  int ox, oy;
  void text(const uint8_t* font, const char* str, int x, int y, uint8_t datum, uint32_t fg, uint32_t bg = COL_BG) {
    s.loadFont(font);
    s.setTextDatum(datum);
    s.setTextColor(c565(fg), c565(bg));
    s.drawString(str, x + ox, y + oy);
    s.unloadFont();
  }
  int width(const uint8_t* font, const char* str) {
    s.loadFont(font);
    int w = s.textWidth(str);
    s.unloadFont();
    return w;
  }
};

// Next Pacific local midnight strictly after t.
int64_t next_local_midnight(int64_t t) {
  pt_tm_t l;
  pt_to_pacific(t, &l);
  for (int off = 7; off <= 8; off++) {
    int64_t cand = pt_utc_epoch(l.year, l.month, l.day + 1, 0, 0, 0) + off * 3600LL;
    pt_tm_t c;
    pt_to_pacific(cand, &c);
    if (c.hour == 0 && c.min == 0) return cand;
  }
  return pt_utc_epoch(l.year, l.month, l.day + 1, 8, 0, 0);
}

void short_time(int64_t t, char* buf, size_t n, bool with_day) {
  static const char* const D[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  pt_tm_t l;
  pt_to_pacific(t, &l);
  const int h12 = l.hour % 12 == 0 ? 12 : l.hour % 12;
  if (with_day) snprintf(buf, n, "%s %d:%02d %s", D[l.wday], h12, l.min, l.hour < 12 ? "AM" : "PM");
  else snprintf(buf, n, "%d:%02d %s", h12, l.min, l.hour < 12 ? "AM" : "PM");
}

// Observed history (dimmer) then forecast, both coloured by river level (green /
// yellow / red bands). The x axis runs from the first observed hour to the last
// forecast hour, whatever that span is.
void draw_chart(Ctx& c, const river_model_t& m, int64_t now) {
  const int X0 = 30, X1 = 420, Y0 = 302, Y1 = 466;
  TFT_eSprite& s = c.s;
  const int ox = c.ox, oy = c.oy;
  s.fillRoundRect(X0 - 8 + ox, Y0 - 12 + oy, X1 - X0 + 16, Y1 - Y0 + 22, 10, c565(COL_PANEL));

  struct Series { const int16_t* v; int n; int64_t t0; int h0; };   // value i is at t0 + (i + h0) h
  const Series hist{m.hist_cft, APP_HIST_HOURS + 1, m.hist_t0, -APP_HIST_HOURS};
  const Series fc{m.chart_cft, APP_CHART_HOURS, m.res.window_start, 0};
  const bool use_fc = m.has_result && m.res.status != FC_STATUS_NONE;

  int lo = INT16_MAX, hi = INT16_MIN;
  int64_t t_start = INT64_MAX, t_end = INT64_MIN;
  auto scan = [&](const Series& sr) {
    for (int i = 0; i < sr.n; i++) {
      const int v = sr.v[i];
      if (v == APP_CHART_NONE) continue;
      const int64_t t = sr.t0 + (int64_t)(i + sr.h0) * 3600;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
      if (t < t_start) t_start = t;
      if (t > t_end) t_end = t;
    }
  };
  if (m.has_obs) scan(hist);
  if (use_fc) scan(fc);
  if (t_start == INT64_MAX) {
    c.text(font_mont_20, "NO DATA TO CHART", W / 2, (Y0 + Y1) / 2, MC_DATUM, COL_MUTED, COL_PANEL);
    return;
  }
  if (t_end - t_start < 6 * 3600) t_end = t_start + 6 * 3600;
  if (hi - lo < 100) {   // never stretch < 1 ft to full height
    const int mid = (hi + lo) / 2;
    lo = mid - 50;
    hi = mid + 50;
  }
  const int pad = (hi - lo) / 8;
  lo -= pad;
  hi += pad;
  const double span = (double)(t_end - t_start);
  auto X = [&](int64_t t) { return X0 + (int)((double)(t - t_start) * (X1 - X0) / span + 0.5); };
  auto Y = [&](int v) { return Y1 - (int)((long)(v - lo) * (Y1 - Y0) / (hi - lo)); };

  // Local-midnight separators; weekday label centred in each day that is wide enough.
  static const char* const D[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int64_t day_start = t_start;
  for (int guard = 0; guard < 10 && day_start < t_end; guard++) {
    const int64_t mid = next_local_midnight(day_start);
    const int64_t day_end = mid < t_end ? mid : t_end;
    if (X(day_end) - X(day_start) >= 38) {
      pt_tm_t l;
      pt_to_pacific(day_start + 60, &l);
      c.text(font_mont_20, D[l.wday], (X(day_start) + X(day_end)) / 2, Y1 + 14, TC_DATUM, COL_MUTED);
    }
    if (mid < t_end) s.drawFastVLine(X(mid) + ox, Y0 + oy, Y1 - Y0, c565(COL_DIM));
    day_start = mid;
  }

  // Drawn column by column so the colour changes exactly where the line crosses 20 or 28 ft.
  // line_amt < 1 dims the line (history); fill_amt is the area tint under it.
  auto plot = [&](const Series& sr, float line_amt, float fill_amt) {
    int px = -1, py = -1, pv = 0, prev_yy = -1;
    for (int i = 0; i < sr.n; i++) {
      const int v = sr.v[i];
      if (v == APP_CHART_NONE) { px = -1; prev_yy = -1; continue; }
      const int x = X(sr.t0 + (int64_t)(i + sr.h0) * 3600), y = Y(v);
      if (px >= 0 && x > px) {
        for (int xx = px; xx <= x; xx++) {
          const int yy = py + (y - py) * (xx - px) / (x - px);
          const int vv = pv + (v - pv) * (xx - px) / (x - px);
          const uint32_t col = level_color(vv);
          s.drawFastVLine(xx + ox, yy + 1 + oy, Y1 - yy - 1, mix(COL_PANEL, col, fill_amt));
          const int top = prev_yy < 0 ? yy : (prev_yy < yy ? prev_yy : yy);
          const int bot = prev_yy < 0 ? yy : (prev_yy > yy ? prev_yy : yy);
          s.drawFastVLine(xx + ox, top - 1 + oy, bot - top + 2, mix(COL_PANEL, col, line_amt));   // 2 px line
          prev_yy = yy;
        }
      }
      px = x;
      py = y;
      pv = v;
    }
  };
  if (m.has_obs) plot(hist, 0.55f, 0.10f);
  if (use_fc) plot(fc, 1.0f, 0.24f);

  // 20 ft and 28 ft reference lines, when the chart's range reaches them.
  const struct { int cft; const char* label; } refs[] = {{LEVEL_WATCH_CFT, "20 ft"}, {LEVEL_HIGH_CFT, "28 ft"}};
  for (const auto& r : refs) {
    if (r.cft <= lo || r.cft >= hi) continue;
    const int y = Y(r.cft);
    const uint16_t col = mix(COL_PANEL, level_color(r.cft + 1), 0.7f);
    for (int x = X0; x < X1; x += 10) s.drawFastHLine(x + ox, y + oy, 5, col);
    c.text(font_mont_20, r.label, X1, y - 3, BR_DATUM, level_color(r.cft + 1), COL_PANEL);
  }

  // NOW: dashed line + label.
  if (now >= t_start && now <= t_end) {
    const int xn = X(now);
    for (int y = Y0; y < Y1; y += 8) s.drawFastVLine(xn + ox, y + oy, 4, c565(COL_TEXT));
    c.text(font_mont_20, "NOW", xn, Y0 - 10, BC_DATUM, COL_TEXT, COL_PANEL);
  }

  // Latest reading and forecast peak.
  if (m.has_obs && m.obs_t >= t_start && m.obs_t <= t_end) {
    const int x = X(m.obs_t), y = Y((int)lroundf(m.obs_ft * 100.0f));
    s.fillCircle(x + ox, y + oy, 6, c565(COL_PANEL));
    s.fillCircle(x + ox, y + oy, 4, c565(COL_HIST));
  }
  if (use_fc && m.res.peak_t >= t_start && m.res.peak_t <= t_end) {
    const int x = X(m.res.peak_t), y = Y((int)lroundf(m.res.max_ft * 100.0f));
    s.fillCircle(x + ox, y + oy, 7, c565(COL_PANEL));
    s.fillCircle(x + ox, y + oy, 5, c565(COL_TEXT));
  }

  // Scale labels (ft) at the left edge.
  char b[16];
  snprintf(b, sizeof b, "%.1f", hi / 100.0);
  c.text(font_mont_20, b, X0, Y0 - 4, TL_DATUM, COL_MUTED, COL_PANEL);
  snprintf(b, sizeof b, "%.1f", lo / 100.0);
  c.text(font_mont_20, b, X0, Y1 + 2, BL_DATUM, COL_MUTED, COL_PANEL);
}

}  // namespace

void ui_render(TFT_eSprite& s, const river_model_t& m, int64_t now, const UiExtras& x) {
  Ctx c{s, x.shift_x, x.shift_y};
  char buf[96], when[48];
  s.fillSprite(c565(COL_BG));

  // Header
  c.text(font_mont_32, "SAN JOAQUIN RIVER", W / 2, 26, TC_DATUM, COL_TEXT);
  c.text(font_mont_20, "VERNALIS \xC2\xB7 VNSC1", W / 2, 66, TC_DATUM, COL_MUTED);
  if (x.batt_pct >= 0 || x.usb) {
    if (x.batt_pct >= 0) snprintf(buf, sizeof buf, "%d%%%s", x.batt_pct, x.charging ? "+" : "");
    else snprintf(buf, sizeof buf, "USB");
    c.text(font_mont_20, buf, W - 22, 8, TR_DATUM, x.batt_pct >= 0 && x.batt_pct < 15 ? COL_BAD : COL_DIM);
  }
  s.drawFastHLine(60 + c.ox, 100 + c.oy, W - 120, c565(COL_DIM));

  const bool err_after = m.error_t != 0;
  const bool stale = m.has_result && !m.demo && now > 0 && now - m.updated_t > APP_STALE_AFTER_S;
  const bool refreshing = m.refresh_t != 0 && now - m.refresh_t < REFRESH_SHOW_S;
  const bool have_value = m.has_result && m.res.status != FC_STATUS_NONE;

  const char* pill = nullptr;
  uint32_t pill_col = COL_BAD, value_col = COL_TEXT;
  static char pill_buf[64];

  // Window / quality line
  const char* window = nullptr;
  uint32_t window_col = COL_MUTED;
  if (!m.has_result) {
    window = err_after ? "UPDATE FAILED" : "WAITING FOR FIRST FORECAST";
    window_col = err_after ? COL_BAD : COL_MUTED;
    pill = "NO DATA";
  } else if (!have_value) {
    window = "NO FORECAST FOR NEXT 5 DAYS";
    window_col = COL_BAD;
    pill = "FORECAST UNAVAILABLE";
  } else {
    const fc_result_t& r = m.res;
    if (r.flags & FC_FLAG_PEAK_AT_END) {
      snprintf(buf, sizeof buf, "STILL RISING AT %d HR FORECAST END", (int)(r.covered_h + 0.5f));
      window = buf;
      window_col = COL_WARN;
    } else if (r.flags & FC_FLAG_SHORT) {
      snprintf(buf, sizeof buf, "ONLY %d OF 120 HRS FORECAST", (int)(r.covered_h + 0.5f));
      window = buf;
      window_col = COL_WARN;
    } else if (r.flags & FC_FLAG_GAP) {
      window = "NEXT 5 DAYS \xC2\xB7 FORECAST HAS GAPS";
      window_col = COL_WARN;
    } else if (r.flags & FC_FLAG_OLD_ISSUE) {
      snprintf(buf, sizeof buf, "NOAA FORECAST ISSUED %lld HRS AGO", (long long)((m.updated_t - m.issued_t) / 3600));
      window = buf;
      window_col = COL_WARN;
    } else {
      window = "NEXT 5 DAYS";
    }
    if (m.demo) {
      pill = "DEMO \xC2\xB7 NOT A LIVE FORECAST";
      pill_col = COL_DEMO;
    } else if (r.status == FC_STATUS_FULL) {
      pill = "COMPLETE 5-DAY FORECAST";
      pill_col = COL_OK;
    } else {
      pill = "PARTIAL FORECAST";
      pill_col = COL_WARN;
    }
  }

  // Readouts are white. The peak turns yellow above 28 ft or on a significant rise;
  // either number goes grey when the data behind it is stale or the update failed.
  if (have_value && (m.res.max_ft > LEVEL_HIGH_CFT / 100.0f ||
                     (m.has_obs && m.res.max_ft - m.obs_ft >= PEAK_RISE_ALERT_FT)))
    value_col = COL_LVL_WATCH;
  if (m.has_result && err_after) {
    snprintf(pill_buf, sizeof pill_buf, "UPDATE FAILED \xC2\xB7 SHOWING LAST DATA");
    pill = pill_buf;
    pill_col = COL_BAD;
    value_col = COL_MUTED;
  } else if (stale) {
    snprintf(pill_buf, sizeof pill_buf, "STALE \xC2\xB7 %lld HRS OLD", (long long)((now - m.updated_t) / 3600));
    pill = pill_buf;
    pill_col = COL_BAD;
    value_col = COL_MUTED;
  }
  if (refreshing) { pill = "REFRESH REQUESTED"; pill_col = COL_ACCENT; }
  if (x.activity) { pill = x.activity; pill_col = COL_ACCENT; }

  // Two columns: NOW (latest gauge reading) and EXPECTED PEAK (forecast max).
  // Digits sit on a baseline (their top is ~53 px above it at 72 px), well clear of
  // the labels. 72 px keeps every value up to 39.99 ft within a 200 px column; a
  // wider number (100+ ft) drops to one decimal.
  constexpr int CL = 118, CR = 334, LABEL_Y = 112, BASE_Y = 198, SUB_Y = 210, MAX_W = 200;
  s.drawFastVLine(W / 2 + c.ox, 116 + c.oy, 118, c565(COL_DIM));
  c.text(font_mont_20, "NOW", CL, LABEL_Y, TC_DATUM, COL_ACCENT);
  c.text(font_mont_20, "EXPECTED PEAK", CR, LABEL_Y, TC_DATUM, COL_ACCENT);

  auto big = [&](const float* ft, int cx, uint32_t col) {
    char v[16] = "--.--";
    if (ft) {
      snprintf(v, sizeof v, "%.2f", (double)*ft);
      if (c.width(font_digits_72, v) > MAX_W) snprintf(v, sizeof v, "%.1f", (double)*ft);
    }
    c.text(font_digits_72, v, cx, BASE_Y, C_BASELINE, col);
  };

  if (m.has_obs) {
    const bool old = now > 0 && now - m.obs_t > 3 * 3600;   // gauge readings normally every 15 min
    big(&m.obs_ft, CL, old ? COL_MUTED : COL_TEXT);
    short_time(m.obs_t, when, sizeof when, old && now - m.obs_t > 12 * 3600);
    snprintf(buf, sizeof buf, "ft \xC2\xB7 %s", when);
    c.text(font_mont_20, buf, CL, SUB_Y, TC_DATUM, old ? COL_WARN : COL_MUTED);
  } else {
    big(nullptr, CL, COL_DIM);
    c.text(font_mont_20, m.has_result ? "no recent reading" : "ft", CL, SUB_Y, TC_DATUM, COL_DIM);
  }

  if (have_value) {
    big(&m.res.max_ft, CR, value_col);
    if (now > 0 && m.res.peak_t <= now + 3600) snprintf(when, sizeof when, "now");
    else short_time(m.res.peak_t, when, sizeof when, true);
    snprintf(buf, sizeof buf, "ft \xC2\xB7 %s", when);
    c.text(font_mont_20, buf, CR, SUB_Y, TC_DATUM, COL_TEXT);
  } else {
    big(nullptr, CR, COL_DIM);
    c.text(font_mont_20, "ft", CR, SUB_Y, TC_DATUM, COL_DIM);
  }

  c.text(font_mont_26, window, W / 2, 248, TC_DATUM, window_col);

  if (m.has_result || m.has_obs) draw_chart(c, m, now);
  if (!have_value && err_after) {
    c.text(font_mont_20, m.last_error, W / 2, m.has_result || m.has_obs ? 496 : 400, TC_DATUM, COL_BAD);
  } else if (!m.has_result && !m.has_obs && !m.error_t) {
    c.text(font_mont_20, "Connecting to Wi-Fi and NOAA...", W / 2, 400, MC_DATUM, COL_MUTED);
  }

  // Footer
  if (x.hint) {
    c.text(font_mont_20, x.hint, W / 2, 528, TC_DATUM, COL_ACCENT);
  } else if (m.has_result) {
    pt_format_short(m.updated_t, when, sizeof when);
    snprintf(buf, sizeof buf, "Updated %s", when);
    c.text(font_mont_20, buf, W / 2, 528, TC_DATUM, COL_MUTED);
  } else if (m.error_t) {
    c.text(font_mont_20, "Retrying automatically \xC2\xB7 tap to retry now", W / 2, 528, TC_DATUM, COL_MUTED);
  }

  if (pill) {
    s.loadFont(font_mont_20);
    const int pw = s.textWidth(pill) + 36, ph = 36, px = (W - pw) / 2, py = 556;
    const uint16_t pbg = mix(COL_BG, pill_col, 0.18f);
    s.fillRoundRect(px + c.ox, py + c.oy, pw, ph, ph / 2, pbg);
    s.drawRoundRect(px + c.ox, py + c.oy, pw, ph, ph / 2, c565(pill_col));
    s.setTextDatum(MC_DATUM);
    s.setTextColor(c565(pill_col), pbg);
    s.drawString(pill, W / 2 + c.ox, py + ph / 2 + 1 + c.oy);
    s.unloadFont();
  }
}
