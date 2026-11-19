#pragma once
// Deep-sleep bookkeeping for the T4-S3: why we woke, how long we slept and were
// awake, and a small battery log. Everything lives in RTC memory, so it survives
// deep sleep but resets on power-loss, reset or reflash.
//
// Policy (in the sketch): on USB the board stays awake; on battery it shows the
// forecast for 60 s after the last touch, then cuts panel power and deep-sleeps
// until the next 00/06/12/18 Pacific fetch. A timer wake fetches with the screen
// off and goes straight back to sleep; the BOOT button wakes it to look.
#include <Arduino.h>

enum class WakeReason : uint8_t { PowerOn = 0, Timer = 1, Button = 2, Other = 3 };

struct PowerLogEntry {    // one per awake period, written when going to sleep
  uint32_t t;             // unix time at sleep
  uint16_t mv;            // battery mV just before sleep (Wi-Fi off)
  uint16_t awake_s;
  uint8_t reason;         // WakeReason that started this awake period
  uint8_t usb : 1, fetched : 1, fetch_ok : 1;
};

namespace power {

WakeReason begin();                   // call first in setup()
WakeReason reason();
bool resumed();                       // woke from our own deep sleep; RTC state is valid
const char* reason_name(WakeReason r);
void note_fetch(bool ok);

// Logs this awake period, arms BOOT + timer wake and enters deep sleep. Never returns.
// Turn the panel off before calling.
[[noreturn]] void deep_sleep(int64_t secs, int mv, bool usb);

// POWER| summary, POWERLOG| lines and a DRAIN| estimate on Serial.
void report(bool usb, int mv, int pct, bool charging, int64_t next_fetch_in_s);

}  // namespace power
