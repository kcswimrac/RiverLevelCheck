/**
 * ui_init.h - LVGL display and touch input initialization
 *
 * Sets up LVGL for the Waveshare RP2350-Touch-AMOLED-1.75 (466x466)
 * using the Waveshare BSP drivers for QSPI AMOLED and CST9217 touch.
 */
#ifndef UI_INIT_H
#define UI_INIT_H

#include "lvgl.h"

void ui_lvgl_init(void);
lv_disp_t* ui_get_display(void);
lv_indev_t* ui_get_touch(void);

#endif /* UI_INIT_H */
