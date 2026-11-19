/**
 * app.h - Display model + line protocol (pure C, shared with the RP2350
 * USB-fed prototype). The T4-S3 fetches NOAA itself over Wi-Fi and calls
 * app_apply_forecast(); the serial protocol stays for testing:
 *
 *   PING                                   -> PONG|river-forecast|<ver>
 *   TIME|<unix_utc>                        -> OK|TIME|<unix_utc>
 *   FC|BEGIN|<gauge>|<issued_unix>|<units> -> OK|BEGIN   (inject a forecast,
 *   FC|PT|<unix_utc>|<stage_ft>               e.g. a saved NOAA fixture)
 *   FC|END|<count>                         -> RESULT|<FULL|PARTIAL|NONE>|<max>|<peak_unix>|<covered_h>|<flags>|<n_used>
 *   ERR|<message>                          -> OK|ERR
 *   DEMO | DEMO|<now_ft>|<peak_ft>         -> OK|DEMO  (illustrative data)
 *   SELFTEST                               -> SELFTEST|PASS|<n> / SELFTEST|FAIL|...
 *   STATUS                                 -> STATUS|...
 */
#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "forecast.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FW_VERSION     "0.4.0"
#define APP_STALE_AFTER_S  (7LL * 3600)   /* 6 h schedule + 1 h grace     */
#define APP_CHART_HOURS    121            /* hourly samples, now .. +120 h */
#define APP_CHART_NONE     INT16_MIN
#define APP_HIST_HOURS     24             /* observed history shown before "now"  */
#define APP_OBS_MAX_AGE_S  (12LL * 3600)  /* older latest reading => no "now" value */

typedef struct {
    bool        has_result;
    bool        demo;
    fc_result_t res;
    char        gauge[12];
    int64_t     issued_t;      /* NOAA issuance time                     */
    int64_t     updated_t;     /* device clock when the result arrived   */
    char        last_error[64];
    int64_t     error_t;       /* 0 = no error since last good update    */
    int64_t     refresh_t;     /* 0 = no manual refresh pending          */
    uint32_t    seq;           /* bumps on every visible change          */
    int16_t     chart_cft[APP_CHART_HOURS];   /* forecast, hour 0..120 from res.window_start, 0.01 ft */
    /* Observed stage (NOAA gauge readings). */
    bool        has_obs;
    float       obs_ft;        /* latest reading                         */
    int64_t     obs_t;         /* its valid time                         */
    int16_t     hist_cft[APP_HIST_HOURS + 1];  /* observed, hour -24..0 from hist_t0, 0.01 ft */
    int64_t     hist_t0;       /* hour 0 of hist_cft (= res.window_start when a forecast exists) */
} river_model_t;

typedef struct {
    int64_t (*now)(void);
    bool    (*set_time)(int64_t unix_utc);
    void    (*reply)(const char *line);
} app_io_t;

void app_init(river_model_t *m, const app_io_t *io);   /* clears the model   */
void app_attach(const app_io_t *io);                   /* keep model (RTC)   */
void app_handle_line(river_model_t *m, char *line);    /* line is modified   */
/* Evaluate a freshly fetched forecast (pts is sorted in place) and publish it. */
void app_apply_forecast(river_model_t *m, const char *gauge, int64_t issued_t,
                        fc_point_t *pts, int n);
/* Observed readings (any order): sets the latest reading and the 24 h history.
 * Call after app_apply_forecast so the history lines up with the forecast chart. */
void app_apply_observed(river_model_t *m, const fc_point_t *pts, int n);
void app_report_error(river_model_t *m, const char *msg);
void app_request_refresh(river_model_t *m);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
