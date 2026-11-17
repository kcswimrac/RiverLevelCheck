/**
 * ui_river.c - River forecast screen.
 *
 *   SAN JOAQUIN RIVER
 *   VERNALIS • VNSC1
 *   MAX PREDICTED RIVER HEIGHT
 *        12.34 FT            <- 128 px digits
 *   NEXT 5 DAYS              <- or "ONLY 115 OF 120 HRS FORECAST" (amber)
 *   Peak: Friday 3:00 AM
 *   Updated Tue 11/17 9:00 AM PST
 *   [ COMPLETE 5-DAY FORECAST ]   <- status pill
 *
 * The outer ring shows how much of the 120 h window the forecast covers,
 * coloured like the status pill.
 */
#include "ui_river.h"

#include <stdio.h>

#include "lvgl.h"
#include "pacific_time.h"

LV_FONT_DECLARE(font_stage_128);

#define COL_BG      0x000000
#define COL_TEXT    0xF2F4F7
#define COL_MUTED   0x8A94A6
#define COL_DIM     0x2A2F38
#define COL_ACCENT  0x38BDF8
#define COL_OK      0x34D399
#define COL_WARN    0xF5A524
#define COL_BAD     0xF2555A
#define COL_DEMO    0xA78BFA

#define REFRESH_SHOW_S 120

static lv_obj_t *arc, *lbl_caption, *lbl_value, *lbl_unit, *lbl_window, *lbl_peak, *lbl_updated;
static lv_obj_t *pill, *lbl_pill;
static void (*tap_cb)(void);

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_coord_t y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

static void on_click(lv_event_t *e)
{
    (void)e;
    if (tap_cb) tap_cb();
}

void ui_river_create(void (*on_tap)(void))
{
    tap_cb = on_tap;
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_click, LV_EVENT_CLICKED, NULL);

    arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 452, 452);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_DIM), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

    lv_obj_t *t = mk_label(scr, &lv_font_montserrat_24, COL_TEXT, 50);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_label_set_text(t, "SAN JOAQUIN RIVER");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *s = mk_label(scr, &lv_font_montserrat_16, COL_MUTED, 82);
    lv_label_set_text(s, "VERNALIS \xE2\x80\xA2 VNSC1");
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 82);

    lbl_caption = mk_label(scr, &lv_font_montserrat_16, COL_ACCENT, 116);
    lv_obj_set_style_text_letter_space(lbl_caption, 1, 0);
    lv_label_set_text(lbl_caption, "MAX PREDICTED RIVER HEIGHT");
    lv_obj_align(lbl_caption, LV_ALIGN_TOP_MID, 0, 116);

    lbl_value = mk_label(scr, &font_stage_128, COL_TEXT, 120);
    lbl_unit  = mk_label(scr, &lv_font_montserrat_28, COL_MUTED, 0);
    lv_label_set_text(lbl_unit, "FT");

    lbl_window  = mk_label(scr, &lv_font_montserrat_20, COL_MUTED, 262);
    lbl_peak    = mk_label(scr, &lv_font_montserrat_24, COL_TEXT, 294);
    lbl_updated = mk_label(scr, &lv_font_montserrat_16, COL_MUTED, 332);

    pill = lv_obj_create(scr);
    lv_obj_remove_style_all(pill);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 34);
    lv_obj_set_style_radius(pill, 17, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
    lv_obj_set_style_border_width(pill, 2, 0);
    lv_obj_set_style_pad_hor(pill, 16, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 366);
    lbl_pill = lv_label_create(pill);
    lv_obj_set_style_text_font(lbl_pill, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_letter_space(lbl_pill, 1, 0);
    lv_obj_center(lbl_pill);
}

static void set_text_color(lv_obj_t *l, const char *txt, uint32_t color)
{
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
}

static void place_value(void)
{
    /* Centre "12.34 FT" as one group: number, then unit on its baseline. */
    lv_obj_update_layout(lbl_value);
    lv_obj_update_layout(lbl_unit);
    lv_coord_t wv = lv_obj_get_width(lbl_value);
    lv_coord_t wu = lv_obj_get_width(lbl_unit);
    lv_coord_t gap = 8;
    lv_coord_t x0 = (466 - (wv + gap + wu)) / 2;
    lv_obj_align(lbl_value, LV_ALIGN_TOP_LEFT, x0, 120);
    lv_obj_align_to(lbl_unit, lbl_value, LV_ALIGN_OUT_RIGHT_BOTTOM, gap, -30);
}

void ui_river_render(const river_model_t *m, int64_t now)
{
    char buf[64], when[40];
    const char *pill_txt;
    uint32_t pill_col, value_col = COL_TEXT;
    float coverage = 0.0f;

    const bool err_after = m->error_t != 0;
    const bool stale = m->has_result && !m->demo && now > 0 && now - m->updated_t > APP_STALE_AFTER_S;
    const bool refreshing = m->refresh_t != 0 && now - m->refresh_t < REFRESH_SHOW_S;

    if (!m->has_result || m->res.status == FC_STATUS_NONE) {
        lv_label_set_text(lbl_value, "--.--");
        value_col = COL_DIM;
        if (!m->has_result) {
            set_text_color(lbl_window, err_after ? "UPDATE FAILED" : "WAITING FOR FORECAST",
                           err_after ? COL_BAD : COL_MUTED);
            lv_label_set_text(lbl_peak, err_after ? m->last_error : "");
            lv_label_set_text(lbl_updated, err_after ? "" : "Connect USB feed or tap to request");
            pill_txt = "NO DATA";
            pill_col = COL_BAD;
        } else {
            set_text_color(lbl_window, "NO FORECAST FOR NEXT 5 DAYS", COL_BAD);
            lv_label_set_text(lbl_peak, "NOAA returned no usable points");
            pt_format_short(m->updated_t, when, sizeof when);
            snprintf(buf, sizeof buf, "Checked %s", when);
            lv_label_set_text(lbl_updated, buf);
            pill_txt = err_after ? "UPDATE FAILED" : "FORECAST UNAVAILABLE";
            pill_col = COL_BAD;
        }
    } else {
        const fc_result_t *r = &m->res;
        snprintf(buf, sizeof buf, "%.2f", (double)r->max_ft);
        lv_label_set_text(lbl_value, buf);
        coverage = r->covered_h / 120.0f;

        if (r->flags & FC_FLAG_PEAK_AT_END) {
            snprintf(buf, sizeof buf, "STILL RISING AT %d HR FORECAST END", (int)(r->covered_h + 0.5f));
            set_text_color(lbl_window, buf, COL_WARN);
        } else if (r->flags & FC_FLAG_SHORT) {
            snprintf(buf, sizeof buf, "ONLY %d OF 120 HRS FORECAST", (int)(r->covered_h + 0.5f));
            set_text_color(lbl_window, buf, COL_WARN);
        } else if (r->flags & FC_FLAG_GAP) {
            set_text_color(lbl_window, "NEXT 5 DAYS \xE2\x80\xA2 FORECAST HAS GAPS", COL_WARN);
        } else if (r->flags & FC_FLAG_OLD_ISSUE) {
            snprintf(buf, sizeof buf, "NOAA FORECAST ISSUED %lld HRS AGO",
                     (long long)((m->updated_t - m->issued_t) / 3600));
            set_text_color(lbl_window, buf, COL_WARN);
        } else {
            set_text_color(lbl_window, "NEXT 5 DAYS", COL_MUTED);
        }

        pt_format_day_time(r->peak_t, when, sizeof when);
        snprintf(buf, sizeof buf, "Peak: %s", when);
        lv_label_set_text(lbl_peak, buf);

        pt_format_short(m->updated_t, when, sizeof when);
        snprintf(buf, sizeof buf, "Updated %s", when);
        lv_label_set_text(lbl_updated, buf);

        if (m->demo) {
            pill_txt = "DEMO \xE2\x80\xA2 NOT A LIVE FORECAST";
            pill_col = COL_DEMO;
        } else if (r->status == FC_STATUS_FULL) {
            pill_txt = "COMPLETE 5-DAY FORECAST";
            pill_col = COL_OK;
        } else {
            pill_txt = "PARTIAL FORECAST";
            pill_col = COL_WARN;
            value_col = COL_WARN;
        }
    }

    /* Freshness problems override the forecast-quality pill. */
    static char pill_buf[48];
    if (m->has_result && err_after) {
        pill_txt = "UPDATE FAILED \xE2\x80\xA2 LAST DATA SHOWN";
        pill_col = COL_BAD;
        value_col = COL_MUTED;
    } else if (stale) {
        snprintf(pill_buf, sizeof pill_buf, "STALE \xE2\x80\xA2 %lld HRS OLD",
                 (long long)((now - m->updated_t) / 3600));
        pill_txt = pill_buf;
        pill_col = COL_BAD;
        value_col = COL_MUTED;
    }
    if (refreshing) {
        pill_txt = "REFRESH REQUESTED...";
        pill_col = COL_ACCENT;
    }

    lv_obj_set_style_text_color(lbl_value, lv_color_hex(value_col), 0);
    lv_label_set_text(lbl_pill, pill_txt);
    lv_obj_set_style_text_color(lbl_pill, lv_color_hex(pill_col), 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(pill_col), 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(pill_col), 0);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 366);

    if (coverage < 0) coverage = 0;
    if (coverage > 1) coverage = 1;
    lv_arc_set_angles(arc, 0, (uint16_t)(coverage * 360.0f + 0.5f));
    lv_obj_set_style_arc_color(arc, lv_color_hex(pill_col), LV_PART_INDICATOR);

    place_value();
}
