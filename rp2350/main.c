/**
 * main.c - San Joaquin River @ Vernalis (VNSC1) 120-hour peak display.
 *
 * Phase 1-3 development firmware for the Waveshare RP2350-Touch-AMOLED-1.75.
 * The board has no Wi-Fi, so a computer runs tools/noaa_feed.py, which
 * fetches the NOAA NWPS forecast and streams the raw (time, stage) points
 * over USB serial. The board itself finds the 120-hour maximum
 * (src/forecast.c), decides FULL / PARTIAL / NONE, and draws the screen.
 *
 * Tap the screen to send REQ|REFRESH to the host (manual refresh).
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "AMOLED_1in75.h"
#include "CST9217.h"
#include "DEV_Config.h"
#include "qspi_pio.h"
#include "lvgl.h"

#include "app.h"
#include "clock.h"
#include "ui_init.h"
#include "ui_river.h"

#define LINE_MAX_LEN      160
#define UI_TICK_MS        30000   /* re-evaluate staleness twice a minute */
#define AMOLED_BRIGHTNESS 120

static river_model_t model;
static bool tap_pending;

static void reply(const char *line)
{
    printf("%s\n", line);
    fflush(stdout);
}

static const app_io_t io = {
    .now      = clock_now,
    .set_time = clock_set,
    .reply    = reply,
};

static void on_tap(void) { tap_pending = true; }

static void init_hardware(void)
{
    DEV_Module_Init();          /* stdio USB, 200 MHz, DMA, I2C1 */
    QSPI_GPIO_Init(qspi);
    QSPI_PIO_Init(qspi);
    QSPI_4Wrie_Mode(&qspi);     /* (sic) vendor spelling */
    AMOLED_1IN75_Init();
    AMOLED_1IN75_SetBrightness(AMOLED_BRIGHTNESS);
    AMOLED_1IN75_Clear(0x0000);
    CST9217_Init();
}

static void poll_serial(void)
{
    static char line[LINE_MAX_LEN];
    static size_t len;
    static bool overflow;

    for (;;) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) return;
        if (c == '\r') continue;
        if (c == '\n') {
            line[len] = '\0';
            if (overflow) reply("NAK|line too long");
            else if (len) app_handle_line(&model, line);
            len = 0;
            overflow = false;
        } else if (len < LINE_MAX_LEN - 1) {
            line[len++] = (char)c;
        } else {
            overflow = true;
        }
    }
}

int main(void)
{
    init_hardware();
    clock_init();
    ui_lvgl_init();

    app_init(&model, &io);
    ui_river_create(on_tap);
    ui_river_render(&model, clock_now());
    lv_task_handler();

    reply("READY|river-forecast|" APP_FW_VERSION);
    printf("INFO|rtc=%s|clock=%lld\n", clock_rtc_present() ? "ok" : "missing",
           (long long)clock_now());

    uint32_t rendered_seq = model.seq;
    absolute_time_t next_tick = make_timeout_time_ms(UI_TICK_MS);

    while (true) {
        poll_serial();

        if (tap_pending) {
            tap_pending = false;
            app_request_refresh(&model);
            reply("REQ|REFRESH");   /* tools/noaa_usb_feed.py --watch fetches on this */
        }

        bool tick = absolute_time_diff_us(get_absolute_time(), next_tick) <= 0;
        if (tick || model.seq != rendered_seq) {
            ui_river_render(&model, clock_now());
            rendered_seq = model.seq;
            next_tick = make_timeout_time_ms(UI_TICK_MS);
        }

        lv_task_handler();
        sleep_ms(5);
    }
}
