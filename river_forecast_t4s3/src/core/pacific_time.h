/**
 * pacific_time.h - UTC <-> America/Los_Angeles without a tz database.
 *
 * US rule since 2007: PDT from the 2nd Sunday of March 02:00 PST
 * (10:00 UTC) until the 1st Sunday of November 02:00 PDT (09:00 UTC).
 * Pure C, mirrored by pacific_offset() in tools/noaa_feed.py.
 */
#ifndef PACIFIC_TIME_H
#define PACIFIC_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  year, month, day;   /* month 1..12 */
    int  hour, min, sec;
    int  wday;               /* 0 = Sunday  */
    bool dst;
} pt_tm_t;

int64_t pt_days_from_civil(int y, int m, int d);
int64_t pt_utc_epoch(int y, int m, int d, int hh, int mm, int ss);
void    pt_utc_to_civil(int64_t t, pt_tm_t *out);       /* plain UTC breakdown */
void    pt_to_pacific(int64_t utc, pt_tm_t *out);      /* local Pacific time   */

/* "Friday 3:00 AM" */
void pt_format_day_time(int64_t utc, char *buf, size_t n);
/* "Tue 11/17 6:00 PM PST" */
void pt_format_short(int64_t utc, char *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PACIFIC_TIME_H */
