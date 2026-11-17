/**
 * clock.h - Wall clock (Unix UTC) for the RP2350 board.
 *
 * Source of truth is the host's TIME command; the on-board PCF85063A RTC
 * (I2C1, 0x51) keeps it across resets. In Phase 5 the RTC alarm/INT pin is
 * what will wake the MCU at 00/06/12/18 Pacific.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stdint.h>

void    clock_init(void);           /* after DEV_Module_Init (I2C up)      */
int64_t clock_now(void);            /* 0 if the time has never been set    */
bool    clock_set(int64_t unix_utc);
bool    clock_rtc_present(void);

#endif /* CLOCK_H */
