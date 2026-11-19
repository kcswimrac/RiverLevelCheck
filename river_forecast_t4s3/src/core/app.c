#include "app.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "selftest.h"

static const app_io_t *io;

/* Receive buffer for a serial-injected FC transfer. */
static fc_point_t rx_pts[FC_MAX_POINTS];
static int        rx_n, rx_bad;
static bool       rx_overflow, rx_active;
static char       rx_gauge[12];
static int64_t    rx_issued;

static void replyf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void replyf(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    io->reply(buf);
}

static int split(char *s, char **f, int max)
{
    int n = 0;
    f[n++] = s;
    for (char *p = s; *p && n < max; p++) {
        if (*p == '|') { *p = '\0'; f[n++] = p + 1; }
    }
    return n;
}

static bool parse_i64(const char *s, int64_t *out)
{
    char *end;
    long long v = strtoll(s, &end, 10);
    if (end == s || *end) return false;
    *out = (int64_t)v;
    return true;
}

static bool parse_f(const char *s, float *out)
{
    char *end;
    float v = strtof(s, &end);
    if (end == s || *end) return false;
    *out = v;
    return true;
}

void app_attach(const app_io_t *iop) { io = iop; }

void app_init(river_model_t *m, const app_io_t *iop)
{
    io = iop;
    memset(m, 0, sizeof *m);
    strcpy(m->gauge, "VNSC1");
    for (int i = 0; i < APP_CHART_HOURS; i++) m->chart_cft[i] = APP_CHART_NONE;
    for (int i = 0; i <= APP_HIST_HOURS; i++) m->hist_cft[i] = APP_CHART_NONE;
}

static bool stage_ok(float ft) { return isfinite(ft) && ft >= FC_STAGE_MIN_FT && ft <= FC_STAGE_MAX_FT; }

/* Hourly history bins, each holding the reading closest to its hour mark. */
static void apply_obs_at(river_model_t *m, const fc_point_t *pts, int n, int64_t now)
{
    const int64_t t0 = m->has_result ? m->res.window_start : now;
    int32_t dist[APP_HIST_HOURS + 1];
    for (int i = 0; i <= APP_HIST_HOURS; i++) { m->hist_cft[i] = APP_CHART_NONE; dist[i] = INT32_MAX; }
    int64_t best_t = 0;
    float best_ft = 0;
    int bins = 0;
    for (int i = 0; i < n; i++) {
        const fc_point_t *p = &pts[i];
        if (!stage_ok(p->stage_ft) || p->t > now + 600) continue;   /* no readings from the future */
        if (p->t > best_t) { best_t = p->t; best_ft = p->stage_ft; }
        const int64_t rel = p->t - t0;                                /* <= 0 for history */
        if (rel < -(APP_HIST_HOURS * 3600LL + 1800) || rel > 1800) continue;
        const int h = (int)((rel + APP_HIST_HOURS * 3600LL + 1800) / 3600);   /* 0..24, 24 = t0 */
        const int64_t d = rel - (int64_t)(h - APP_HIST_HOURS) * 3600;
        const int32_t ad = (int32_t)(d < 0 ? -d : d);
        if (h < 0 || h > APP_HIST_HOURS || ad >= dist[h]) continue;
        if (dist[h] == INT32_MAX) bins++;
        dist[h] = ad;
        m->hist_cft[h] = (int16_t)lroundf(p->stage_ft * 100.0f);
    }
    m->hist_t0 = t0;
    m->has_obs = best_t > 0 && now - best_t <= APP_OBS_MAX_AGE_S;
    m->obs_ft  = m->has_obs ? best_ft : 0;
    m->obs_t   = m->has_obs ? best_t : 0;
    m->seq++;
    replyf("OBS|%s|%.2f|%lld|%d", m->has_obs ? "OK" : "NONE", (double)m->obs_ft, (long long)m->obs_t, bins);
}

void app_apply_observed(river_model_t *m, const fc_point_t *pts, int n)
{
    apply_obs_at(m, pts, n, io->now());
}

static void build_chart(river_model_t *m, const fc_point_t *pts, int n)
{
    for (int i = 0; i < APP_CHART_HOURS; i++) m->chart_cft[i] = APP_CHART_NONE;
    const int64_t t0 = m->res.window_start;
    for (int i = 0; i < n; i++) {
        const fc_point_t *p = &pts[i];
        if (!stage_ok(p->stage_ft)) continue;
        int64_t rel = p->t - t0;
        if (rel < -FC_LOOKBACK_S || rel > FC_HORIZON_S) continue;
        int h = rel < 0 ? 0 : (int)((rel + 1800) / 3600);
        if (h >= APP_CHART_HOURS) continue;
        m->chart_cft[h] = (int16_t)lroundf(p->stage_ft * 100.0f);
    }
}

static void apply_at(river_model_t *m, const char *gauge, int64_t issued_t, fc_point_t *pts, int n,
                     int64_t now)
{
    fc_compute(pts, n, issued_t, now, &m->res);
    build_chart(m, pts, n);
    m->has_result = true;
    m->demo       = false;
    m->issued_t   = issued_t;
    m->updated_t  = now;
    m->error_t    = 0;
    m->last_error[0] = '\0';
    m->refresh_t  = 0;
    snprintf(m->gauge, sizeof m->gauge, "%s", gauge);
    m->seq++;
    replyf("RESULT|%s|%.2f|%lld|%.1f|%lu|%d", fc_status_name(m->res.status), (double)m->res.max_ft,
           (long long)m->res.peak_t, (double)m->res.covered_h, (unsigned long)m->res.flags,
           m->res.n_used);
}

void app_apply_forecast(river_model_t *m, const char *gauge, int64_t issued_t, fc_point_t *pts, int n)
{
    apply_at(m, gauge, issued_t, pts, n, io->now());
}

void app_report_error(river_model_t *m, const char *msg)
{
    snprintf(m->last_error, sizeof m->last_error, "%s", msg && *msg ? msg : "update failed");
    m->error_t = io->now();
    if (m->error_t <= 0) m->error_t = 1;
    m->refresh_t = 0;
    m->seq++;
}

/* Illustrative data: a reading of now_ft after a slow 24 h rise, and a forecast
 * that climbs to peak_ft about 58 h out. */
static void set_demo(river_model_t *m, float now_ft, float peak_ft)
{
    int64_t now = io->now();
    if (now <= 0) now = 1700000000;   /* values are illustrative anyway */
    static fc_point_t demo[121];
    for (int h = 0; h <= 120; h++) {
        float x = (float)(h - 58) / 22.0f;
        demo[h].t = now + h * 3600LL;
        demo[h].stage_ft = now_ft + (peak_ft - now_ft) * expf(-x * x);
    }
    apply_at(m, "VNSC1", now, demo, 121, now);   /* works before the clock is set */
    static fc_point_t hist[97];                   /* 24 h of 15-min readings, slowly rising */
    for (int i = 0; i <= 96; i++) {
        hist[i].t = now - (96 - i) * 900LL;
        hist[i].stage_ft = now_ft - 0.70f * (float)(96 - i) / 96.0f + 0.05f * sinf((float)(i - 96) / 6.0f);
    }
    apply_obs_at(m, hist, 97, now);
    m->demo = true;
    m->seq++;
}

static void fc_end(river_model_t *m, char **f, int nf)
{
    int64_t count;
    if (!rx_active) { io->reply("NAK|END without BEGIN"); return; }
    rx_active = false;
    if (nf < 3 || !parse_i64(f[2], &count)) { io->reply("NAK|bad END"); return; }
    if (rx_overflow) { replyf("NAK|too many points (max %d)", FC_MAX_POINTS); return; }
    if (rx_bad || count != rx_n) {
        replyf("NAK|transfer error: got %d of %lld, %d bad", rx_n, (long long)count, rx_bad);
        return;
    }
    if (io->now() <= 0) { io->reply("NAK|clock not set (send TIME first)"); return; }
    app_apply_forecast(m, rx_gauge, rx_issued, rx_pts, rx_n);
}

void app_handle_line(river_model_t *m, char *line)
{
    char *f[6];
    int nf = split(line, f, 6);

    if (!strcmp(f[0], "FC") && nf >= 2) {
        if (!strcmp(f[1], "PT")) {
            if (!rx_active) return;
            fc_point_t p;
            if (nf < 4 || !parse_i64(f[2], &p.t) || !parse_f(f[3], &p.stage_ft)) { rx_bad++; return; }
            if (rx_n >= FC_MAX_POINTS) { rx_overflow = true; return; }
            rx_pts[rx_n++] = p;
        } else if (!strcmp(f[1], "BEGIN")) {
            rx_active = false;
            if (nf < 5 || !parse_i64(f[3], &rx_issued)) { io->reply("NAK|bad BEGIN"); return; }
            if (strcmp(f[4], "ft")) { replyf("NAK|units '%s' (stage must be ft)", f[4]); return; }
            snprintf(rx_gauge, sizeof rx_gauge, "%s", f[2]);
            rx_n = rx_bad = 0;
            rx_overflow = false;
            rx_active = true;
            io->reply("OK|BEGIN");
        } else if (!strcmp(f[1], "END")) {
            fc_end(m, f, nf);
        } else {
            io->reply("NAK|unknown FC record");
        }
    } else if (!strcmp(f[0], "TIME")) {
        int64_t t;
        if (nf < 2 || !parse_i64(f[1], &t) || !io->set_time(t)) { io->reply("NAK|bad TIME"); return; }
        replyf("OK|TIME|%lld", (long long)t);
    } else if (!strcmp(f[0], "PING")) {
        io->reply("PONG|river-forecast|" APP_FW_VERSION);
    } else if (!strcmp(f[0], "ERR")) {
        app_report_error(m, nf >= 2 ? f[1] : "");
        io->reply("OK|ERR");
    } else if (!strcmp(f[0], "DEMO")) {
        float now_ft = 9.10f, peak_ft = 12.34f;   /* DEMO|<now_ft>|<peak_ft> picks the values */
        if (nf >= 3 && (!parse_f(f[1], &now_ft) || !parse_f(f[2], &peak_ft) || !stage_ok(now_ft) ||
                        !stage_ok(peak_ft))) {
            io->reply("NAK|usage: DEMO or DEMO|<now_ft>|<peak_ft>");
            return;
        }
        set_demo(m, now_ft, peak_ft);
        io->reply("OK|DEMO");
    } else if (!strcmp(f[0], "SELFTEST")) {
        selftest_run(io->reply);
    } else if (!strcmp(f[0], "STATUS")) {
        replyf("STATUS|now=%lld|has=%d|demo=%d|status=%s|max=%.2f|peak=%lld|covered_h=%.1f|flags=%lu|"
               "updated=%lld|issued=%lld|obs=%.2f|obs_t=%lld|err=%s",
               (long long)io->now(), m->has_result, m->demo, fc_status_name(m->res.status),
               (double)m->res.max_ft, (long long)m->res.peak_t, (double)m->res.covered_h,
               (unsigned long)m->res.flags, (long long)m->updated_t, (long long)m->issued_t,
               (double)m->obs_ft, (long long)m->obs_t, m->error_t ? m->last_error : "-");
    } else if (f[0][0]) {
        io->reply("NAK|unknown command");
    }
}

void app_request_refresh(river_model_t *m)
{
    int64_t now = io->now();
    m->refresh_t = now > 0 ? now : 1;
    m->seq++;
}
