#include "Power.h"

#include <esp_sleep.h>
#include <esp_timer.h>
#include <string.h>
#include <time.h>

namespace {

constexpr uint32_t MAGIC = 0x52565031;   // "RVP1"
constexpr int LOG_N = 64;                // ~2 weeks at 4-5 wakes a day

struct Stats {
  uint32_t magic;
  uint32_t boots, wakes_timer, wakes_button, wakes_other;
  uint32_t fetch_ok, fetch_fail;
  uint64_t awake_ms;
  uint64_t asleep_s;
  int64_t sleep_started_t;   // time() when we last went to sleep
  PowerLogEntry log[LOG_N];
  uint8_t head, count;
};

RTC_DATA_ATTR Stats st;

WakeReason g_reason = WakeReason::PowerOn;
bool g_resumed = false;
bool g_fetched = false, g_fetch_ok = false;

int64_t uptime_ms() { return esp_timer_get_time() / 1000; }

}  // namespace

namespace power {

WakeReason begin() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: g_reason = WakeReason::Timer; break;
    case ESP_SLEEP_WAKEUP_EXT0: g_reason = WakeReason::Button; break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: g_reason = WakeReason::PowerOn; break;   // power-on / reset / flash
    default: g_reason = WakeReason::Other; break;
  }
  g_resumed = g_reason != WakeReason::PowerOn && st.magic == MAGIC;
  if (!g_resumed) {
    memset(&st, 0, sizeof st);
    st.magic = MAGIC;
  }
  if (g_resumed && st.sleep_started_t) {
    const int64_t slept = (int64_t)time(nullptr) - st.sleep_started_t;
    if (slept > 0) st.asleep_s += slept;
  }
  switch (g_reason) {
    case WakeReason::Timer: st.wakes_timer++; break;
    case WakeReason::Button: st.wakes_button++; break;
    case WakeReason::Other: st.wakes_other++; break;
    default: st.boots++; break;
  }
  return g_reason;
}

WakeReason reason() { return g_reason; }
bool resumed() { return g_resumed; }

const char* reason_name(WakeReason r) {
  switch (r) {
    case WakeReason::Timer: return "timer";
    case WakeReason::Button: return "button";
    case WakeReason::Other: return "other";
    default: return "power-on";
  }
}

void note_fetch(bool ok) {
  g_fetched = true;
  g_fetch_ok = ok;
  (ok ? st.fetch_ok : st.fetch_fail)++;
}

void deep_sleep(int64_t secs, int mv, bool usb) {
  const int64_t up = uptime_ms();
  PowerLogEntry& e = st.log[st.head];
  e.t = (uint32_t)time(nullptr);
  e.mv = mv > 0 && mv < 65535 ? (uint16_t)mv : 0;
  e.awake_s = up / 1000 > 65535 ? 65535 : (uint16_t)(up / 1000);
  e.reason = (uint8_t)g_reason;
  e.usb = usb;
  e.fetched = g_fetched;
  e.fetch_ok = g_fetch_ok;
  st.head = (st.head + 1) % LOG_N;
  if (st.count < LOG_N) st.count++;
  st.awake_ms += up;
  st.sleep_started_t = time(nullptr);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);   // BOOT button (external pull-up)
  esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
  esp_deep_sleep_start();
}

void report(bool usb, int mv, int pct, bool charging, int64_t next_fetch_in_s) {
  const uint64_t awake_ms = st.awake_ms + uptime_ms();
  const uint64_t total_ms = awake_ms + st.asleep_s * 1000ULL;
  const double duty = total_ms ? 100.0 * (double)awake_ms / (double)total_ms : 100.0;
  Serial.printf("POWER|%s|mv=%d|pct=%d|charging=%d|wake=%s|boots=%lu|timer=%lu|button=%lu|awake=%llus|asleep=%llus|"
                "duty=%.2f%%|fetch_ok=%lu|fetch_fail=%lu|next_fetch=%llds\n",
                usb ? "USB" : "BATTERY", mv, pct, charging ? 1 : 0, reason_name(g_reason), (unsigned long)st.boots,
                (unsigned long)st.wakes_timer, (unsigned long)st.wakes_button,
                (unsigned long long)(awake_ms / 1000), (unsigned long long)st.asleep_s, duty,
                (unsigned long)st.fetch_ok, (unsigned long)st.fetch_fail, (long long)next_fetch_in_s);

  // Oldest first. The last battery-only run gives a rough drain rate.
  int first_batt = -1, last_batt = -1;
  for (int i = 0; i < st.count; i++) {
    const PowerLogEntry& e = st.log[(st.head + LOG_N - st.count + i) % LOG_N];
    Serial.printf("POWERLOG|%lu|%u|%u|%s|%s|%s\n", (unsigned long)e.t, e.mv, e.awake_s,
                  reason_name((WakeReason)e.reason), e.usb ? "usb" : "batt",
                  e.fetched ? (e.fetch_ok ? "fetch-ok" : "fetch-fail") : "-");
    if (e.usb || e.mv == 0) {
      first_batt = last_batt = -1;
    } else {
      if (first_batt < 0) first_batt = i;
      last_batt = i;
    }
  }
  if (first_batt >= 0 && last_batt > first_batt) {
    const PowerLogEntry& a = st.log[(st.head + LOG_N - st.count + first_batt) % LOG_N];
    const PowerLogEntry& b = st.log[(st.head + LOG_N - st.count + last_batt) % LOG_N];
    const double hours = (double)(b.t - a.t) / 3600.0;
    if (hours >= 6.0) {
      const double per_day = (double)((int)a.mv - (int)b.mv) / hours * 24.0;
      Serial.printf("DRAIN|%.1f mV/day over %.1f h (%u -> %u mV)\n", per_day, hours, a.mv, b.mv);
    }
  }
}

}  // namespace power
