#include "selftest.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "forecast.h"
#include "pacific_time.h"

#define H 3600LL

static fc_point_t pts[FC_MAX_POINTS];
static int n_pts;
static int n_fail, n_pass;
static void (*out)(const char *);

static void add(int64_t t, float s) { pts[n_pts].t = t; pts[n_pts].stage_ft = s; n_pts++; }

/* Hourly points from h0..h1 (inclusive) relative to now, triangle peak at hp. */
static void hourly(int64_t now, int h0, int h1, int hp, float peak)
{
    for (int h = h0; h <= h1; h++) {
        float d = (float)(h > hp ? h - hp : hp - h);
        add(now + h * H, peak - 0.05f * d);
    }
}

static void check(const char *name, int ok)
{
    char buf[96];
    if (ok) { n_pass++; return; }
    n_fail++;
    snprintf(buf, sizeof buf, "SELFTEST|FAIL|%s", name);
    out(buf);
}

static void expect(const char *name, int64_t issued, int64_t now, fc_status_t st, uint32_t flags,
                   float max_ft, int64_t peak_t)
{
    fc_result_t r;
    fc_compute(pts, n_pts, issued, now, &r);
    int ok = r.status == st && r.flags == flags;
    if (st != FC_STATUS_NONE) ok = ok && fabsf(r.max_ft - max_ft) < 0.001f && r.peak_t == peak_t;
    check(name, ok);
}

static void expect_str(const char *name, const char *got, const char *want)
{
    char buf[120];
    if (!strcmp(got, want)) { n_pass++; return; }
    n_fail++;
    snprintf(buf, sizeof buf, "SELFTEST|FAIL|%s got '%s' want '%s'", name, got, want);
    out(buf);
}

bool selftest_run(void (*reply)(const char *line))
{
    out = reply;
    n_fail = n_pass = 0;
    const int64_t now = pt_utc_epoch(2026, 11, 17, 17, 0, 0);
    char s[48];

    /* 1. complete hourly 0..120 h, peak 10.00 at +50 h */
    n_pts = 0; hourly(now, 0, 120, 50, 10.0f);
    expect("full", now - 2 * H, now, FC_STATUS_FULL, 0, 10.0f, now + 50 * H);

    /* 2. ends at +115 h (NOAA's VNSC1 product often does) */
    n_pts = 0; hourly(now, 0, 115, 50, 10.0f);
    expect("short", now, now, FC_STATUS_PARTIAL, FC_FLAG_SHORT, 10.0f, now + 50 * H);

    /* 3. last point at +119 h is accepted as complete (hourly tolerance) */
    n_pts = 0; hourly(now, 0, 119, 50, 10.0f);
    expect("tolerance", now, now, FC_STATUS_FULL, 0, 10.0f, now + 50 * H);

    /* 4. nothing at all */
    n_pts = 0;
    expect("empty", now, now, FC_STATUS_NONE, 0, 0, 0);

    /* 5. only points outside the window */
    n_pts = 0; add(now - 5 * H, 20.0f); add(now + 121 * H, 30.0f);
    expect("outside", now, now, FC_STATUS_NONE, 0, 0, 0);

    /* 6. 10 h hole inside the window */
    n_pts = 0; hourly(now, 0, 40, 20, 9.0f); hourly(now, 50, 120, 100, 8.0f);
    expect("gap", now, now, FC_STATUS_PARTIAL, FC_FLAG_GAP, 9.0f, now + 20 * H);

    /* 7. complete data but NOAA issuance is 40 h old */
    n_pts = 0; hourly(now, 0, 120, 50, 10.0f);
    expect("old issue", now - 40 * H, now, FC_STATUS_PARTIAL, FC_FLAG_OLD_ISSUE, 10.0f, now + 50 * H);

    /* 8. short forecast still rising at its end */
    n_pts = 0; hourly(now, 0, 100, 100, 12.0f);
    expect("rising at end", now, now, FC_STATUS_PARTIAL, FC_FLAG_SHORT | FC_FLAG_PEAK_AT_END,
           12.0f, now + 100 * H);

    /* 9. plateau: earliest time of the max wins */
    n_pts = 0; hourly(now, 0, 120, 200, 5.0f);   /* rising all window */
    for (int i = 0; i < n_pts; i++) if (pts[i].t >= now + 110 * H) pts[i].stage_ft = 7.0f;
    expect("plateau", now, now, FC_STATUS_FULL, 0, 7.0f, now + 110 * H);

    /* 10. look-back: -30 min counts, -2 h does not */
    n_pts = 0; hourly(now, 1, 120, 60, 6.0f); add(now - 2 * H, 50.0f); add(now - H / 2, 9.0f);
    expect("lookback", now, now, FC_STATUS_FULL, 0, 9.0f, now - H / 2);

    /* 11. unsorted input */
    n_pts = 0;
    for (int h = 120; h >= 0; h--) add(now + h * H, h == 33 ? 4.2f : 3.0f);
    expect("unsorted", now, now, FC_STATUS_FULL, 0, 4.2f, now + 33 * H);

    /* 12. NaN / absurd values ignored */
    n_pts = 0; hourly(now, 0, 120, 10, 3.0f); add(now + 5 * H, NAN); add(now + 6 * H, 9999.0f);
    expect("bad values", now, now, FC_STATUS_FULL, 0, 3.0f, now + 10 * H);

    /* 13. first point more than 6 h out */
    n_pts = 0; hourly(now, 8, 120, 60, 6.0f);
    expect("late start", now, now, FC_STATUS_PARTIAL, FC_FLAG_GAP, 6.0f, now + 60 * H);

    /* Pacific time / DST edges (verified against IANA America/Los_Angeles) */
    pt_format_short(pt_utc_epoch(2026, 11, 17, 17, 0, 0), s, sizeof s);
    expect_str("pst", s, "Tue 11/17 9:00 AM PST");
    pt_format_short(pt_utc_epoch(2026, 11, 1, 8, 59, 0), s, sizeof s);
    expect_str("pdt end-1m", s, "Sun 11/1 1:59 AM PDT");
    pt_format_short(pt_utc_epoch(2026, 11, 1, 9, 0, 0), s, sizeof s);
    expect_str("pst start", s, "Sun 11/1 1:00 AM PST");
    pt_format_short(pt_utc_epoch(2026, 3, 8, 9, 59, 0), s, sizeof s);
    expect_str("pst end-1m", s, "Sun 3/8 1:59 AM PST");
    pt_format_short(pt_utc_epoch(2026, 3, 8, 10, 0, 0), s, sizeof s);
    expect_str("pdt start", s, "Sun 3/8 3:00 AM PDT");
    pt_format_day_time(pt_utc_epoch(2026, 11, 20, 11, 0, 0), s, sizeof s);
    expect_str("day time", s, "Friday 3:00 AM");
    pt_format_day_time(pt_utc_epoch(2028, 3, 1, 7, 30, 0), s, sizeof s);
    expect_str("leap", s, "Tuesday 11:30 PM");   /* 2028-02-29 local */

    char buf[48];
    if (n_fail == 0) snprintf(buf, sizeof buf, "SELFTEST|PASS|%d", n_pass);
    else snprintf(buf, sizeof buf, "SELFTEST|FAIL|%d of %d failed", n_fail, n_fail + n_pass);
    reply(buf);
    return n_fail == 0;
}
