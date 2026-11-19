// River forecast display - LilyGo T4-S3 2.41" AMOLED (ESP32-S3, Wi-Fi).
//
// Shows the highest NOAA-predicted stage for the San Joaquin River at Vernalis
// (gauge VNSC1) over the next 120 hours. The board fetches the forecast itself
// at 12 AM, 6 AM, 12 PM and 6 PM Pacific, plus at boot and on a tap or BOOT press.
// Wi-Fi is switched off between fetches.
//
// Power: the screen stays on, on USB and on battery. Deep sleep between fetches is
// built in but off (SLEEP_ON_BATTERY): with it on, the board shows the forecast for
// 60 s after the last touch on battery, then turns the panel off and sleeps until
// the next fetch; BOOT wakes it. The forecast and schedule survive in RTC memory.
//
// Wi-Fi: up to 5 networks are saved; each fetch joins the strongest saved one in
// range. Setup on the touchscreen opens by itself when nothing is saved; a tap
// opens it when no saved network can be joined; hold the screen (or BOOT) for 3 s
// to open it any time. Pick a network, type the password, JOIN (saved networks
// join with one tap).
//
// Forecast logic (src/core) is shared, unchanged, with the RP2350 prototype.
// Serial console (115200, newline):
//   WIFI|<ssid>|<password>   save a network (NVS, up to 5) and fetch now
//   WIFI?                    list saved networks
//   WIFI-|<ssid>             forget a saved network
//   FETCH                    fetch now
//   POWER                    sleep/wake stats and battery log
//   SLEEP | SLEEP|<secs>     deep-sleep now (until next fetch, or for <secs>)
//   PWRMODE|BATTERY|AUTO     BATTERY = act as if unplugged until next wake (testing)
//   DEMO | SELFTEST | STATUS | PING | TIME|<unix> | FC|... (see src/core/app.h)
//
// Build: see README.md (arduino-cli), or Arduino IDE with
//   Board ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash 16MB,
//   Partition "16M Flash (3MB APP/9.9MB FATFS)", PSRAM: OPI PSRAM.

#include <esp_sleep.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "Board.h"
#include "NoaaFetch.h"
#include "Power.h"
#include "RiverUi.h"
#include "WifiSetup.h"
#include "WifiStore.h"
#if __has_include("secrets.h")   // optional, git-ignored; see secrets_example.h
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif
#include "src/core/app.h"
#include "src/core/pacific_time.h"

static const char* GAUGE = "VNSC1";

static constexpr int BOOT_BUTTON_PIN = 0;
static constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
static constexpr uint32_t NTP_TIMEOUT_MS = 15000;
static constexpr int64_t RETRY_DELAY_MS = 10LL * 60 * 1000;   // after a failed fetch
static constexpr int MAX_RETRIES = 3;                         // per scheduled slot
static constexpr int64_t MANUAL_MIN_GAP_MS = 20000;           // between manual fetches
static constexpr int64_t LONG_PRESS_MS = 3000;                // opens Wi-Fi setup
static constexpr int64_t UI_TICK_MS = 30000;
static constexpr int64_t SHIFT_PERIOD_S = 600;                // burn-in shift every 10 min
static constexpr uint8_t BRIGHT_DAY = 150, BRIGHT_NIGHT = 40; // night = 10 PM - 6 AM
static constexpr bool SLEEP_ON_BATTERY = false;              // true: deep-sleep between fetches on battery
static constexpr int64_t AWAKE_BATTERY_MS = 60000;            // screen time on battery after last touch
static constexpr int64_t AWAKE_SETUP_MS = 180000;             // ... while Wi-Fi setup is open
static constexpr int LOW_BATT_MV = 3400;                      // on battery below this: no Wi-Fi fetches
static constexpr int64_t MIN_SLOT_GAP_S = 1800;               // skip a slot that is < 30 min away
static constexpr int64_t EARLY_WAKE_S = 900;                  // timer wake this close to a fetch = fetch now

static Board board;
static WifiStore wifi_store;
static WifiSetup wifi_setup;
static fc_point_t fetch_pts[FC_MAX_POINTS];
static constexpr int OBS_MAX = 160;                           // 25 h of 15-min readings fits easily
static fc_point_t obs_pts[OBS_MAX];

// Kept in RTC memory across deep sleep (reset on power-on / reflash).
RTC_DATA_ATTR static river_model_t model;
RTC_DATA_ATTR static int64_t next_fetch_t = 0;   // time() of next fetch; 0 = nothing scheduled
RTC_DATA_ATTR static int retries = 0;
RTC_DATA_ATTR static bool wifi_problem = false;  // not configured, or the saved network can't be joined

static int64_t last_manual_ms = -MANUAL_MIN_GAP_MS;
static bool fetch_requested = false;
static bool screen_on = true;          // false during a battery timer wake (fetch in the dark)
static bool force_battery = false;     // PWRMODE|BATTERY, until the next wake
static bool ignore_press = false;      // the BOOT press that woke us
static int64_t last_activity_ms = 0;

// ---------------------------------------------------------------- clock / io
static int64_t mono_ms() { return esp_timer_get_time() / 1000; }
static int64_t raw_now() { return (int64_t)time(nullptr); }   // runs through deep sleep, valid or not

static int64_t clock_now() {
  time_t t = time(nullptr);
  return t > 1704067200 ? (int64_t)t : 0;   // valid only after NTP (2024+)
}

static bool clock_set(int64_t t) {
  if (t < 1704067200LL || t > 4102444800LL) return false;
  timeval tv{(time_t)t, 0};
  settimeofday(&tv, nullptr);
  return true;
}

static void reply(const char* line) { Serial.println(line); }

static const app_io_t io = {clock_now, clock_set, reply};

// ---------------------------------------------------------------- scheduling
// Seconds from `now` until the next 00/06/12/18 Pacific slot (DST-aware).
static int64_t secs_to_next_slot(int64_t now) {
  pt_tm_t l;
  pt_to_pacific(now, &l);
  int64_t secs = (int64_t)((l.hour / 6 + 1) * 6 - l.hour) * 3600 - l.min * 60 - l.sec;
  // On DST-change days the local clock jumps; nudge until we land on a slot.
  for (int i = 0; i < 2; i++) {
    pt_tm_t c;
    pt_to_pacific(now + secs, &c);
    if (c.min == 0 && c.hour % 6 == 0) break;
    secs += (c.hour % 6 == 5) ? 3600 : -3600;
  }
  return secs > 60 ? secs : 60;
}

// Next slot at least 30 min away, so a fetch just before a slot (or a timer wake
// that fires a little early) doesn't fetch again minutes later.
static int64_t secs_to_following_slot(int64_t now) {
  return now ? secs_to_next_slot(now + MIN_SLOT_GAP_S) + MIN_SLOT_GAP_S : 6 * 3600;   // no clock: 6 h
}

// Success: next slot. Failure: retry every 10 min, up to 3 times, then wait for the next slot.
static void schedule_after(bool ok) {
  if (!ok && retries < MAX_RETRIES) {
    retries++;
    next_fetch_t = raw_now() + RETRY_DELAY_MS / 1000;
  } else {
    retries = 0;
    next_fetch_t = raw_now() + secs_to_following_slot(clock_now());
  }
  const int64_t in_s = next_fetch_t - raw_now();
  Serial.printf("[SCHED] next fetch in %lldh%02lldm\n", (long long)(in_s / 3600), (long long)((in_s % 3600) / 60));
}

// ---------------------------------------------------------------- display
static UiExtras extras;
static int batt_mv = 0;
static uint32_t drawn_seq = UINT32_MAX;
static int64_t next_ui_ms = 0;

static void read_power() { board.battery(batt_mv, extras.batt_pct, extras.charging, extras.usb); }

static bool on_battery() { return force_battery || !extras.usb; }
static bool battery_low() { return on_battery() && batt_mv > 2500 && batt_mv < LOW_BATT_MV; }

static void poll_power() {
  static int64_t last = 0;
  if (mono_ms() - last < 2000) return;
  last = mono_ms();
  const bool was_usb = extras.usb;
  read_power();
  if (was_usb && !extras.usb) last_activity_ms = mono_ms();   // just unplugged: full 60 s
}

static void update_extras() {
  read_power();
  const int64_t now = clock_now();
  static const int8_t SX[9] = {0, 3, 3, 0, -3, -3, -3, 0, 3}, SY[9] = {0, 0, 3, 3, 3, 0, -3, -3, -3};
  const int k = now ? (int)((now / SHIFT_PERIOD_S) % 9) : 0;
  extras.shift_x = SX[k];
  extras.shift_y = SY[k];
  extras.hint = wifi_problem  ? "Tap to choose a Wi-Fi network"
                : battery_low() ? "Battery low - charge to keep updating"
                                : nullptr;
  if (now) {
    pt_tm_t l;
    pt_to_pacific(now, &l);
    board.setBrightness(l.hour >= 22 || l.hour < 6 ? BRIGHT_NIGHT : BRIGHT_DAY);
  }
}

static void render(const char* activity = nullptr) {
  if (!screen_on) {   // battery timer wake: nobody is looking
    drawn_seq = model.seq;
    return;
  }
  if (wifi_setup.active()) {
    board.setBrightness(BRIGHT_DAY);
    wifi_setup.render(board.canvas());
  } else {
    extras.activity = activity;
    update_extras();
    ui_render(board.canvas(), model, clock_now(), extras);
  }
  board.flush();
  drawn_seq = model.seq;
  next_ui_ms = mono_ms() + UI_TICK_MS;
}

// ---------------------------------------------------------------- Wi-Fi + fetch
static bool wifi_join(const String& ssid, const String& pass, char* err, size_t n,
                      uint32_t timeout_ms = WIFI_TIMEOUT_MS) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  const int64_t t0 = mono_ms();
  while (WiFi.status() != WL_CONNECTED && mono_ms() - t0 < timeout_ms) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(err, n, "Wi-Fi: can't join \"%s\"", ssid.c_str());
    return false;
  }
  Serial.printf("[WIFI] connected to %s, IP %s, RSSI %d\n", ssid.c_str(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  return true;
}

// Join the best saved network: the ones in range, strongest first. If none shows
// up in the scan, try each saved one briefly in case it is hidden.
static bool wifi_connect_any(char* err, size_t n) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  const int found = WiFi.scanNetworks(false, true);
  int order[WifiStore::MAX], rssi[WifiStore::MAX], k = 0;
  for (int i = 0; i < wifi_store.count(); i++) {
    int best = INT32_MIN;
    for (int j = 0; j < found; j++)
      if (WiFi.SSID(j) == wifi_store.ssid(i) && WiFi.RSSI(j) > best) best = WiFi.RSSI(j);
    if (best == INT32_MIN) continue;
    int at = k++;   // insertion sort, strongest first
    while (at > 0 && rssi[at - 1] < best) {
      order[at] = order[at - 1];
      rssi[at] = rssi[at - 1];
      at--;
    }
    order[at] = i;
    rssi[at] = best;
  }
  WiFi.scanDelete();
  const bool in_range = k > 0;
  Serial.printf("[WIFI] scan: %d networks, %d of %d saved in range\n", found < 0 ? 0 : found, k,
                wifi_store.count());
  if (!in_range)
    for (int i = 0; i < wifi_store.count(); i++) order[k++] = i;
  for (int t = 0; t < k; t++) {
    const String ssid = wifi_store.ssid(order[t]), pass = wifi_store.pass(order[t]);
    if (wifi_join(ssid, pass, err, n, in_range ? WIFI_TIMEOUT_MS : 10000)) {
      wifi_store.used(ssid);
      return true;
    }
  }
  if (!in_range) snprintf(err, n, "No saved Wi-Fi network in range");
  return false;
}

static void wifi_off() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

static bool ntp_sync(char* err, size_t n) {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");   // UTC; display does Pacific
  const int64_t t0 = mono_ms();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && mono_ms() - t0 < NTP_TIMEOUT_MS) delay(100);
  if (clock_now() == 0) {
    snprintf(err, n, "Clock: NTP time sync failed");
    return false;
  }
  return true;
}

static void do_fetch() {
  char err[64] = "";
  bool ok = false;
  bool joined = WiFi.status() == WL_CONNECTED;   // e.g. straight after on-screen setup
  if (!joined) {
    if (wifi_store.count() == 0) {
      snprintf(err, sizeof err, "No Wi-Fi network set up");
      wifi_problem = true;
    } else {
      render("CONNECTING TO WI-FI...");
      joined = wifi_connect_any(err, sizeof err);
      wifi_problem = !joined;
    }
  }
  if (joined) {
    wifi_problem = false;
    render("UPDATING FROM NOAA...");
    if (ntp_sync(err, sizeof err)) {
      int64_t issued = 0;
      const int n = noaa_fetch_forecast(GAUGE, fetch_pts, FC_MAX_POINTS, &issued, err, sizeof err);
      if (n >= 0) {
        app_apply_forecast(&model, GAUGE, issued, fetch_pts, n);
        ok = true;
      }
      // Current height + 24 h history. Optional: a failure here keeps the forecast.
      char oerr[64] = "";
      const int no = noaa_fetch_observed(GAUGE, clock_now() - (APP_HIST_HOURS + 1) * 3600LL, obs_pts, OBS_MAX,
                                         oerr, sizeof oerr);
      if (no >= 0) app_apply_observed(&model, obs_pts, no);
      else Serial.printf("[NOAA] observed failed: %s\n", oerr);
    }
  }
  wifi_off();
  if (!ok) {
    Serial.printf("[FETCH] failed: %s\n", err);
    app_report_error(&model, err);
  }
  power::note_fetch(ok);
  schedule_after(ok);
  last_activity_ms = mono_ms();   // keep the result on screen for the full 60 s
  render();
}

// ---------------------------------------------------------------- sleep
// secs <= 0: until the next scheduled fetch.
[[noreturn]] static void enter_deep_sleep(int64_t secs = 0) {
  if (secs <= 0) secs = next_fetch_t ? next_fetch_t - raw_now() : 6 * 3600;
  if (secs < 30) secs = 30;
  wifi_off();
  while (digitalRead(BOOT_BUTTON_PIN) == LOW) delay(10);   // a held BOOT would wake us at once
  read_power();
  Serial.printf("[SLEEP] %lldh%02lldm (battery %d mV); press BOOT to wake\n", (long long)(secs / 3600),
                (long long)((secs % 3600) / 60), batt_mv);
  Serial.flush();
  delay(20);
  board.sleep();
  power::deep_sleep(secs, batt_mv, extras.usb);
}

// On USB: never. On battery (only with SLEEP_ON_BATTERY, or PWRMODE|BATTERY for
// testing): 60 s after the last touch (3 min in setup); straight away after a
// screen-off timer wake has done its fetch.
static void maybe_sleep() {
  if (!on_battery() || !(SLEEP_ON_BATTERY || force_battery)) {
    last_activity_ms = mono_ms();
    return;
  }
  if (fetch_requested) return;
  const int64_t limit = wifi_setup.active() ? AWAKE_SETUP_MS : screen_on ? AWAKE_BATTERY_MS : 0;
  if (mono_ms() - last_activity_ms >= limit) enter_deep_sleep();
}

static void open_wifi_setup(const String& note = "") {
  wifi_setup.open(wifi_store.count() > 0, note);
  Serial.println("[SETUP] Wi-Fi setup opened");
  render();
}

static void handle_setup_action(WifiSetup::Action a) {
  if (a == WifiSetup::Action::Cancel) {
    wifi_off();   // the scan left the radio on
    render();
    return;
  }
  if (a != WifiSetup::Action::Join) return;
  wifi_setup.showJoining();
  render();
  char err[64];
  if (wifi_join(wifi_setup.ssid(), wifi_setup.password(), err, sizeof err)) {
    wifi_store.remember(wifi_setup.ssid(), wifi_setup.password());
    Serial.printf("[SETUP] saved network \"%s\"\n", wifi_setup.ssid().c_str());
    wifi_setup.close();
    wifi_problem = false;
    retries = 0;
    do_fetch();   // reuses the open connection
  } else {
    wifi_off();
    wifi_setup.joinFailed("Couldn't join. Check the password and try again.");
    render();
  }
}

// ---------------------------------------------------------------- input
static void handle_wifi_command(const String& line) {
  if (line == "WIFI?") {
    Serial.printf("WIFI|%d saved|radio %s\n", wifi_store.count(), WiFi.status() == WL_CONNECTED ? "connected" : "off");
    for (int i = 0; i < wifi_store.count(); i++) Serial.printf("WIFI|%d|%s\n", i + 1, wifi_store.ssid(i).c_str());
    return;
  }
  if (line.startsWith("WIFI-|")) {
    const String ssid = line.substring(6);
    Serial.printf("%s|WIFI-|%s\n", wifi_store.forget(ssid) ? "OK" : "NAK", ssid.c_str());
    return;
  }
  // WIFI|ssid|password  (password may itself contain '|')
  const int a = line.indexOf('|');
  const int b = line.indexOf('|', a + 1);
  if (a < 0 || b < 0 || b == a + 1) {
    Serial.println("NAK|usage: WIFI|<ssid>|<password>");
    return;
  }
  wifi_store.remember(line.substring(a + 1, b), line.substring(b + 1));
  Serial.printf("OK|WIFI|%s\n", line.substring(a + 1, b).c_str());
  wifi_setup.close();
  retries = 0;
  fetch_requested = true;
}

static void poll_serial() {
  static String line;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (line.length() < 200) line += ch;
      continue;
    }
    line.trim();
    last_activity_ms = mono_ms();
    if (line.startsWith("WIFI")) {
      handle_wifi_command(line);
    } else if (line == "FETCH") {
      fetch_requested = true;
    } else if (line == "SETUP") {
      open_wifi_setup();
    } else if (line == "POWER") {
      read_power();
      power::report(extras.usb, batt_mv, extras.batt_pct, extras.charging,
                    next_fetch_t ? next_fetch_t - raw_now() : -1);
    } else if (line == "SLEEP" || line.startsWith("SLEEP|")) {
      const long secs = line.length() > 6 ? line.substring(6).toInt() : 0;
      Serial.println("OK|SLEEP");
      enter_deep_sleep(secs);
    } else if (line.startsWith("PWRMODE|")) {
      force_battery = line.endsWith("BATTERY");
      last_activity_ms = mono_ms();
      Serial.printf("OK|PWRMODE|%s\n", force_battery ? "BATTERY" : "AUTO");
    } else if (line.length()) {
      char buf[210];
      line.toCharArray(buf, sizeof buf);
      app_handle_line(&model, buf);
    }
    line = "";
  }
}

// Touch + BOOT button. In setup: every finger-down is a key/row press.
// Otherwise: short tap = refresh (or open setup if Wi-Fi is the problem), hold 3 s = setup.
static void poll_input() {
  static bool was_down = false, long_fired = false;
  static int64_t down_at = 0, last_poll = 0;
  if (mono_ms() - last_poll < 30) return;
  last_poll = mono_ms();

  int x = 0, y = 0;
  const bool touch = board.touchPoint(x, y);
  const bool button = digitalRead(BOOT_BUTTON_PIN) == LOW;
  const bool down = touch || button;
  if (down) last_activity_ms = mono_ms();

  if (ignore_press || (down && !screen_on)) {   // a press only wakes; act on the next one
    if (!screen_on) {
      screen_on = true;
      render();
    }
    ignore_press = down;
    was_down = false;
    return;
  }

  if (down && !was_down) {
    down_at = mono_ms();
    long_fired = false;
    if (wifi_setup.active() && touch) {
      handle_setup_action(wifi_setup.onTouch(x, y));
    }
  }
  if (down && !long_fired && !wifi_setup.active() && mono_ms() - down_at >= LONG_PRESS_MS) {
    long_fired = true;
    open_wifi_setup();
  }
  if (!down && was_down && !long_fired && !wifi_setup.active() && mono_ms() - down_at < 1000) {
    if (wifi_problem) {
      open_wifi_setup();
    } else if (mono_ms() - last_manual_ms >= MANUAL_MIN_GAP_MS) {
      last_manual_ms = mono_ms();
      Serial.println("REQ|REFRESH");
      app_request_refresh(&model);
      retries = 0;
      fetch_requested = true;
    }
  }
  was_down = down;
}

// ---------------------------------------------------------------- Arduino
void setup() {
  const WakeReason why = power::begin();
  const bool resumed = power::resumed();
  Serial.begin(115200);
  if (!resumed) delay(300);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (resumed) {
    app_attach(&io);   // forecast, schedule and Wi-Fi state came through sleep
  } else {
    app_init(&model, &io);
    next_fetch_t = 0;
    retries = 0;
    wifi_problem = false;
  }
  wifi_store.begin(WIFI_SSID, WIFI_PASSWORD);
  wifi_setup.setStore(&wifi_store);
  if (!board.begin()) Serial.println("[BOOT] display failed; continuing headless");
  read_power();
  screen_on = !(why == WakeReason::Timer && on_battery() && SLEEP_ON_BATTERY);
  if (!screen_on) board.setBrightness(0);
  ignore_press = why == WakeReason::Button;
  last_activity_ms = mono_ms();
  Serial.printf("READY|river-forecast|" APP_FW_VERSION "|t4-s3|wake=%s\n", power::reason_name(why));

  if (wifi_store.count() == 0) {
    wifi_problem = true;
    if (screen_on) open_wifi_setup();
    return;
  }
  render();
  const int64_t due_in = next_fetch_t ? next_fetch_t - raw_now() : 0;
  if (!resumed || next_fetch_t == 0) {
    fetch_requested = true;   // first fetch right away
  } else if (why == WakeReason::Timer && due_in < EARLY_WAKE_S) {
    fetch_requested = true;   // loop() skips it if the battery is low
  } else if (why == WakeReason::Button && due_in <= 0 && !wifi_problem) {
    fetch_requested = true;   // missed a slot somehow
  }
}

void loop() {
  poll_serial();
  poll_input();
  poll_power();

  if (wifi_setup.active()) {
    if (wifi_setup.needsScan()) {
      render();   // shows "Scanning..."
      wifi_setup.scan();
    }
    if (wifi_setup.dirty()) render();
    maybe_sleep();
    delay(10);
    return;   // no scheduled fetches while the user is in setup
  }

  if (fetch_requested || (next_fetch_t && raw_now() >= next_fetch_t)) {
    fetch_requested = false;
    if (battery_low() && !screen_on) {   // keep the schedule, save the cell
      Serial.println("[POWER] battery low; skipping this fetch");
      next_fetch_t = raw_now() + secs_to_following_slot(clock_now());
    } else {
      do_fetch();
    }
  }

  if (model.seq != drawn_seq || mono_ms() >= next_ui_ms) render();
  maybe_sleep();
  delay(10);
}
