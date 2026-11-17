/**
 * ui_init.c - LVGL display and touch initialization for RP2350-Touch-AMOLED-1.75
 *
 * Based on Waveshare's LVGL_example.c pattern.  Sets up:
 *   - LVGL tick via Pico SDK repeating timer (5 ms)
 *   - Display driver with partial-update flush to QSPI AMOLED
 *   - Touch input driver reading CST9217 capacitive touch
 *
 * Target: Waveshare RP2350-Touch-AMOLED-1.75 (466 x 466, RGB565)
 */

#include "ui_init.h"

#include "DEV_Config.h"
#include "AMOLED_1in75.h"
#include "CST9217.h"
#include "qspi_pio.h"
#include "lvgl.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

/* ==========================================================================
 *  STATIC BUFFERS AND DRIVERS
 * ========================================================================== */

/* Display buffer: 20 scan-lines at 466 px wide = 9 320 pixels (~18 KB) */
#define DISP_BUF_LINES  20
static lv_color_t disp_buf1[466 * DISP_BUF_LINES];

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t      disp_drv;
static lv_indev_drv_t     indev_drv;

/* Handles returned by LVGL registration */
static lv_disp_t*  registered_disp  = NULL;
static lv_indev_t* registered_indev = NULL;

/* Pico SDK repeating timer for LVGL tick */
static struct repeating_timer tick_timer;

/* ==========================================================================
 *  LVGL TICK CALLBACK (5 ms)
 * ========================================================================== */

static bool lvgl_tick_cb(struct repeating_timer *t) {
    (void)t;
    lv_tick_inc(5);
    return true;  /* keep repeating */
}

/* ==========================================================================
 *  DISPLAY FLUSH CALLBACK
 * ========================================================================== */

/**
 * Push a rectangular area of pixels to the AMOLED via DMA over QSPI PIO.
 *
 * IMPORTANT: We cannot use AMOLED_1IN75_DisplayWindows() because it assumes
 * the Image pointer is a full 466x466 framebuffer and indexes with absolute
 * screen coordinates.  LVGL provides a compact packed buffer containing only
 * the dirty rectangle pixels, so we DMA it row by row with local offsets.
 */
static void disp_flush_cb(lv_disp_drv_t *drv,
                           const lv_area_t *area,
                           lv_color_t *color_p)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    /* Set the AMOLED hardware window (Xend/Yend are exclusive in BSP) */
    AMOLED_1IN75_SetWindows(area->x1, area->y1,
                            area->x1 + w, area->y1 + h);

    /* Begin pixel write command */
    QSPI_Select(qspi);
    QSPI_Pixel_Write(qspi, 0x2c);

    /* DMA the LVGL buffer row by row.
     * color_p is packed: row0, row1, ... rowN — each row is w pixels.
     * We use local offsets (0, w*2, 2*w*2, ...) NOT absolute screen coords. */
    uint8_t *buf = (uint8_t *)color_p;
    channel_config_set_dreq(&c, pio_get_dreq(qspi.pio, qspi.sm, true));
    for (uint32_t y = 0; y < h; y++) {
        dma_channel_configure(dma_tx, &c,
                              &qspi.pio->txf[qspi.sm],   /* dest: PIO TX FIFO */
                              buf + y * w * 2,            /* src: local row offset */
                              w * 2,                      /* transfer count (bytes) */
                              true);                      /* start immediately */
        while (dma_channel_is_busy(dma_tx));
    }

    QSPI_Deselect(qspi);
    lv_disp_flush_ready(drv);
}

/* ==========================================================================
 *  TOUCH READ CALLBACK
 * ========================================================================== */

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    CST9217_Read_Data();

    if (CST9217.points > 0 && CST9217.data[0].valid) {
        data->point.x = CST9217.data[0].x;
        data->point.y = CST9217.data[0].y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ==========================================================================
 *  PUBLIC API
 * ========================================================================== */

/**
 * Initialise LVGL, register the display and touch drivers, and start the
 * tick timer.  Call this once after the Waveshare BSP has been initialised
 * (DEV_Module_Init, AMOLED_1IN75_Init, CST9217_Init).
 */
void ui_lvgl_init(void)
{
    /* ---- Core ---- */
    lv_init();

    /* ---- Tick source (5 ms repeating timer on Pico SDK alarm pool) ---- */
    add_repeating_timer_ms(5, lvgl_tick_cb, NULL, &tick_timer);

    /* ---- Display draw buffer ---- */
    lv_disp_draw_buf_init(&draw_buf, disp_buf1, NULL, 466 * DISP_BUF_LINES);

    /* ---- Display driver ---- */
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = 466;
    disp_drv.ver_res      = 466;
    disp_drv.flush_cb     = disp_flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.full_refresh  = 0;  /* partial updates for efficiency */

    registered_disp = lv_disp_drv_register(&disp_drv);

    /* ---- Touch input driver ---- */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;

    registered_indev = lv_indev_drv_register(&indev_drv);
}

lv_disp_t* ui_get_display(void)
{
    return registered_disp;
}

lv_indev_t* ui_get_touch(void)
{
    return registered_indev;
}
