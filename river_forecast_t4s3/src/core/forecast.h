/**
 * forecast.h - 120-hour peak-stage calculation for NOAA NWPS forecasts.
 *
 * Pure C99, no Pico SDK dependency, so it can be reused unchanged on the
 * ESP32-C3 in Phase 4. tools/noaa_feed.py contains a line-for-line Python
 * reference (fc_reference) that the firmware result is checked against.
 */
#ifndef FORECAST_H
#define FORECAST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FC_MAX_POINTS       512
#define FC_HORIZON_S        (120LL * 3600)  /* look-ahead window: 120 hours        */
#define FC_LOOKBACK_S       (3600LL)        /* include the most recent hourly point */
#define FC_END_TOLERANCE_S  (3600LL)        /* hourly data: last point >= 119 h ok  */
#define FC_MAX_GAP_S        (6LL * 3600)    /* > 6 h between points => gap          */
#define FC_MAX_ISSUE_AGE_S  (36LL * 3600)   /* NOAA issuance older than this => old */
#define FC_STAGE_MIN_FT     (-50.0f)
#define FC_STAGE_MAX_FT     (200.0f)

typedef struct {
    int64_t t;          /* valid time, Unix seconds UTC */
    float   stage_ft;   /* NOAA "primary" (Stage, ft)    */
} fc_point_t;

typedef enum {
    FC_STATUS_NONE = 0,  /* no usable forecast points inside the window   */
    FC_STATUS_FULL,      /* complete, current, gap-free 120-hour forecast */
    FC_STATUS_PARTIAL,   /* usable, but see flags: not a full 5-day view  */
} fc_status_t;

#define FC_FLAG_SHORT        0x01u  /* forecast ends before now + 120 h            */
#define FC_FLAG_GAP          0x02u  /* hole > 6 h inside the window (or at start)  */
#define FC_FLAG_OLD_ISSUE    0x04u  /* NOAA issuance older than 36 h               */
#define FC_FLAG_PEAK_AT_END  0x08u  /* short forecast still rising at its last pt  */

typedef struct {
    fc_status_t status;
    uint32_t    flags;
    float       max_ft;        /* highest predicted stage in the window       */
    int64_t     peak_t;        /* earliest time that max is reached            */
    int64_t     window_start;  /* now                                          */
    int64_t     window_end;    /* now + 120 h                                  */
    int64_t     last_t;        /* last forecast point inside the window        */
    float       covered_h;     /* hours from now to last_t (0..120)            */
    int         n_used;        /* points inside the window                     */
} fc_result_t;

/* Sorts pts in place by time (insertion sort; n <= FC_MAX_POINTS).
 * issued_t <= 0 means "issuance time unknown" (not flagged as old). */
void fc_compute(fc_point_t *pts, int n, int64_t issued_t, int64_t now, fc_result_t *out);

const char *fc_status_name(fc_status_t s);

#ifdef __cplusplus
}
#endif

#endif /* FORECAST_H */
