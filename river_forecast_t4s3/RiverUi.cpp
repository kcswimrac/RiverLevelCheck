// River forecast screen for the 450x600 portrait AMOLED.
//
//   SAN JOAQUIN RIVER                       battery
//   VERNALIS · VNSC1
//   ─────────────────────────────
//   MAX PREDICTED RIVER HEIGHT
//          12.34 FT                 <- 150 px digits
//   NEXT 5 DAYS                     <- amber when the forecast is short/gappy/old
//   Peak: Friday 3:00 AM
//   [ hourly stage chart, now .. +120 h, peak marked, missing hours hatched ]
//   Updated Tue 11/17 9:00 AM PST
//   ( COMPLETE 5-DAY FORECAST )     <- status pill
#include "RiverUi.h"

#include <stdio.h>

#include "src/core/pacific_time.h"
#include "src/fonts/fonts.h"

namespace {

constexpr uint32_t COL_BG = 0x000000, COL_TEXT = 0xF2F4F7, COL_MUTED = 0x8A94A6, COL_DIM = 0x2A2F38,
                   COL_PANEL = 0x0E1218, COL_ACCENT = 0x38BDF8, COL_OK = 0x34D399, COL_WARN = 0xF5A524,
                   COL_BAD = 0xF2555A, COL_DEMO = 0xA78BFA;
constexpr int W = 450;
constexpr int REFRESH_SHOW_S = 120;

uint16_t c565(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}

uint16_t mix(uint32_t a, uint32_t b, float t) {   // t=0 -> a, t=1 -> b
  auto ch = [&](int sh) { return (uint32_t)(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t) << sh; };
  return c565(ch(16) | ch(8) | ch(0));
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

void draw_chart(Ctx& c, const river_model_t& m, uint32_t line_col) {
  const int X0 = 30, X1 = 420, Y0 = 404, Y1 = 494;
  TFT_eSprite& s = c.s;
  const int ox = c.ox, oy = c.oy;
  s.fillRoundRect(X0 - 8 + ox, Y0 - 10 + oy, X1 - X0 + 16, Y1 - Y0 + 20, 10, c565(COL_PANEL));

  int lo = INT16_MAX, hi = INT16_MIN, last_h = -1;
  for (int h = 0; h < APP_CHART_HOURS; h++) {
    int v = m.chart_cft[h];
    if (v == APP_CHART_NONE) continue;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    last_h = h;
  }
  if (last_h < 0) {
    c.text(font_mont_20, "NO FORECAST POINTS", W / 2, (Y0 + Y1) / 2, MC_DATUM, COL_MUTED, COL_PANEL);
    return;
  }
  if (hi - lo < 100) {                     // never stretch < 1 ft to full height
    int mid = (hi + lo) / 2;
    lo = mid - 50;
    hi = mid + 50;
  }
  const int pad = (hi - lo) / 8;
  lo -= pad;
  hi += pad;
  auto X = [&](float h) { return X0 + (int)(h * (X1 - X0) / 120.0f + 0.5f); };
  auto Y = [&](int v) { return Y1 - (int)((long)(v - lo) * (Y1 - Y0) / (hi - lo)); };

  // Local-midnight separators and weekday labels under the chart.
  const int64_t t0 = m.res.window_start;
  int64_t mid = next_local_midnight(t0 - 1);
  int64_t prev = t0;
  for (int i = 0; i < 7; i++) {
    float h = (float)(mid - t0) / 3600.0f;
    float hp = (float)(prev - t0) / 3600.0f;
    float label_h = (hp + (h < 120 ? h : 120)) / 2;
    if (h - hp > 6 && label_h < 120) {
      pt_tm_t l;
      pt_to_pacific(prev + 3600, &l);
      static const char* const D[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      c.text(font_mont_20, D[l.wday], X(label_h), Y1 + 14, TC_DATUM, COL_MUTED);
    }
    if (h >= 120) break;
    if (h > 0) s.drawFastVLine(X(h) + ox, Y0 + oy, Y1 - Y0, c565(COL_DIM));
    prev = mid;
    mid = next_local_midnight(mid);
  }

  // Area + line.
  const uint16_t fill = mix(COL_PANEL, line_col, 0.22f), line = c565(line_col);
  int px = -1, py = -1;
  for (int h = 0; h < APP_CHART_HOURS; h++) {
    int v = m.chart_cft[h];
    if (v == APP_CHART_NONE) { px = -1; continue; }
    int x = X(h), y = Y(v);
    if (px >= 0) {
      for (int xx = px; xx <= x; xx++) {
        int yy = py + (y - py) * (xx - px) / (x - px ? x - px : 1);
        s.drawFastVLine(xx + ox, yy + oy, Y1 - yy, fill);
      }
      s.drawLine(px + ox, py + oy, x + ox, y + oy, line);
      s.drawLine(px + ox, py - 1 + oy, x + ox, y - 1 + oy, line);
    }
    px = x;
    py = y;
  }

  // Hours the forecast does not cover: hatch them so a short forecast is obvious.
  if (last_h < 119) {
    const int xs = X(last_h) + 2;
    for (int k = xs - (Y1 - Y0); k < X1; k += 8)
      for (int d = 0; d <= Y1 - Y0; d++) {
        int xx = k + d;
        if (xx >= xs && xx <= X1) s.drawPixel(xx + ox, Y1 - d + oy, c565(COL_DIM));
      }
    if (X1 - xs > 60) c.text(font_mont_20, "NO DATA", (xs + X1) / 2, Y0 + 4, TC_DATUM, COL_MUTED, COL_PANEL);
  }

  // Peak marker.
  if (m.res.status != FC_STATUS_NONE) {
    float ph = (float)(m.res.peak_t - t0) / 3600.0f;
    if (ph < 0) ph = 0;
    if (ph > 120) ph = 120;
    int v = (int)(m.res.max_ft * 100.0f + (m.res.max_ft >= 0 ? 0.5f : -0.5f));
    int x = X(ph), y = Y(v);
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
  c.text(font_mont_20, "MAX PREDICTED RIVER HEIGHT", W / 2, 116, TC_DATUM, COL_ACCENT);

  const bool err_after = m.error_t != 0;
  const bool stale = m.has_result && !m.demo && now > 0 && now - m.updated_t > APP_STALE_AFTER_S;
  const bool refreshing = m.refresh_t != 0 && now - m.refresh_t < REFRESH_SHOW_S;
  const bool have_value = m.has_result && m.res.status != FC_STATUS_NONE;

  const char* pill = nullptr;
  uint32_t pill_col = COL_BAD, value_col = COL_TEXT, chart_col = COL_ACCENT;
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
      pill_col = chart_col = COL_DEMO;
    } else if (r.status == FC_STATUS_FULL) {
      pill = "COMPLETE 5-DAY FORECAST";
      pill_col = COL_OK;
    } else {
      pill = "PARTIAL FORECAST";
      pill_col = value_col = chart_col = COL_WARN;
    }
  }

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

  // Big number + FT, centred as one group, sharing a baseline.
  const char* value = "--.--";
  char vbuf[16];
  if (have_value) {
    snprintf(vbuf, sizeof vbuf, "%.2f", (double)m.res.max_ft);
    value = vbuf;
  } else {
    value_col = COL_DIM;
  }
  const int baseline = 272;
  const int wv = c.width(font_digits_150, value), wu = c.width(font_mont_32, "FT"), gap = 12;
  const int x0 = (W - (wv + gap + wu)) / 2;
  c.text(font_digits_150, value, x0, baseline, L_BASELINE, value_col);
  c.text(font_mont_32, "FT", x0 + wv + gap, baseline, L_BASELINE, have_value ? COL_MUTED : COL_DIM);

  c.text(font_mont_26, window, W / 2, 292, TC_DATUM, window_col);

  if (have_value) {
    pt_format_day_time(m.res.peak_t, when, sizeof when);
    snprintf(buf, sizeof buf, "Peak: %s", when);
    c.text(font_mont_32, buf, W / 2, 334, TC_DATUM, COL_TEXT);
  } else if (err_after) {
    c.text(font_mont_20, m.last_error, W / 2, 340, TC_DATUM, COL_BAD);
  }

  if (m.has_result) draw_chart(c, m, chart_col);
  else if (!m.error_t) c.text(font_mont_20, "Connecting to Wi-Fi and NOAA...", W / 2, 440, MC_DATUM, COL_MUTED);

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
