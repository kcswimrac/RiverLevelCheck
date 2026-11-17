/**
 * forecast.c - see forecast.h. Keep in sync with fc_reference() in
 * tools/noaa_feed.py; the host compares both results after every update.
 */
#include "forecast.h"

#include <math.h>
#include <string.h>

static void sort_by_time(fc_point_t *pts, int n)
{
    for (int i = 1; i < n; i++) {
        fc_point_t key = pts[i];
        int j = i - 1;
        while (j >= 0 && pts[j].t > key.t) {
            pts[j + 1] = pts[j];
            j--;
        }
        pts[j + 1] = key;
    }
}

static int usable(const fc_point_t *p)
{
    return isfinite(p->stage_ft) && p->stage_ft >= FC_STAGE_MIN_FT && p->stage_ft <= FC_STAGE_MAX_FT;
}

void fc_compute(fc_point_t *pts, int n, int64_t issued_t, int64_t now, fc_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->status       = FC_STATUS_NONE;
    out->window_start = now;
    out->window_end   = now + FC_HORIZON_S;
    if (!pts || n <= 0) return;

    sort_by_time(pts, n);

    const int64_t lo = now - FC_LOOKBACK_S;
    const int64_t hi = now + FC_HORIZON_S;
    int64_t prev_t = 0, first_t = 0;
    float prev_stage = 0.0f, peak_prev_stage = 0.0f;
    int peak_has_prev = 0;
    uint32_t flags = 0;

    for (int i = 0; i < n; i++) {
        const fc_point_t *p = &pts[i];
        if (p->t < lo || p->t > hi || !usable(p)) continue;

        if (out->n_used == 0) {
            first_t = p->t;
            out->max_ft = p->stage_ft;
            out->peak_t = p->t;
            peak_has_prev = 0;
        } else {
            if (p->t - prev_t > FC_MAX_GAP_S) flags |= FC_FLAG_GAP;
            if (p->stage_ft > out->max_ft) {   /* strict: earliest time wins ties */
                out->max_ft = p->stage_ft;
                out->peak_t = p->t;
                peak_prev_stage = prev_stage;
                peak_has_prev = 1;
            }
        }
        prev_t = p->t;
        prev_stage = p->stage_ft;
        out->n_used++;
    }

    if (out->n_used == 0) return;

    out->last_t = prev_t;
    if (first_t - now > FC_MAX_GAP_S) flags |= FC_FLAG_GAP;

    int64_t covered = out->last_t - now;
    if (covered < 0) covered = 0;
    out->covered_h = (float)covered / 3600.0f;

    if (out->last_t < hi - FC_END_TOLERANCE_S) {
        flags |= FC_FLAG_SHORT;
        if (out->peak_t == out->last_t && peak_has_prev && out->max_ft > peak_prev_stage)
            flags |= FC_FLAG_PEAK_AT_END;
    }
    if (issued_t > 0 && now - issued_t > FC_MAX_ISSUE_AGE_S) flags |= FC_FLAG_OLD_ISSUE;

    out->flags  = flags;
    out->status = flags ? FC_STATUS_PARTIAL : FC_STATUS_FULL;
}

const char *fc_status_name(fc_status_t s)
{
    switch (s) {
    case FC_STATUS_FULL:    return "FULL";
    case FC_STATUS_PARTIAL: return "PARTIAL";
    default:                return "NONE";
    }
}
