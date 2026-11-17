#include "pacific_time.h"

#include <stdio.h>

static const char *const WDAY_LONG[7]  = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                          "Thursday", "Friday", "Saturday"};
static const char *const WDAY_SHORT[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

/* Howard Hinnant's days_from_civil / civil_from_days. */
int64_t pt_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t mp  = m > 2 ? m - 3 : m + 9;
    const int64_t doy = (153 * mp + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t pt_utc_epoch(int y, int m, int d, int hh, int mm, int ss)
{
    return pt_days_from_civil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

void pt_utc_to_civil(int64_t t, pt_tm_t *o)
{
    int64_t days = floor_div(t, 86400);
    int64_t secs = t - days * 86400;
    o->hour = (int)(secs / 3600);
    o->min  = (int)((secs % 3600) / 60);
    o->sec  = (int)(secs % 60);
    o->wday = (int)(((days % 7) + 7 + 4) % 7);   /* 1970-01-01 was a Thursday */

    int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp  = (5 * doy + 2) / 153;
    o->day   = (int)(doy - (153 * mp + 2) / 5 + 1);
    o->month = (int)(mp < 10 ? mp + 3 : mp - 9);
    o->year  = (int)(yoe + era * 400 + (o->month <= 2));
    o->dst   = false;
}

static int first_sunday(int y, int m)
{
    int64_t days = pt_days_from_civil(y, m, 1);
    int wday = (int)(((days % 7) + 7 + 4) % 7);
    return 1 + (7 - wday) % 7;
}

void pt_to_pacific(int64_t utc, pt_tm_t *out)
{
    pt_tm_t u;
    pt_utc_to_civil(utc, &u);
    const int y = u.year;
    const int64_t dst_start = pt_utc_epoch(y, 3, first_sunday(y, 3) + 7, 10, 0, 0);
    const int64_t dst_end   = pt_utc_epoch(y, 11, first_sunday(y, 11), 9, 0, 0);
    const bool dst = utc >= dst_start && utc < dst_end;
    pt_utc_to_civil(utc + (dst ? -7 : -8) * 3600LL, out);
    out->dst = dst;
}

static void hour12(int h, int *h12, const char **ampm)
{
    *ampm = h < 12 ? "AM" : "PM";
    *h12 = h % 12;
    if (*h12 == 0) *h12 = 12;
}

void pt_format_day_time(int64_t utc, char *buf, size_t n)
{
    pt_tm_t t;
    int h;
    const char *ap;
    pt_to_pacific(utc, &t);
    hour12(t.hour, &h, &ap);
    snprintf(buf, n, "%s %d:%02d %s", WDAY_LONG[t.wday], h, t.min, ap);
}

void pt_format_short(int64_t utc, char *buf, size_t n)
{
    pt_tm_t t;
    int h;
    const char *ap;
    pt_to_pacific(utc, &t);
    hour12(t.hour, &h, &ap);
    snprintf(buf, n, "%s %d/%d %d:%02d %s %s", WDAY_SHORT[t.wday], t.month, t.day, h, t.min, ap,
             t.dst ? "PDT" : "PST");
}
