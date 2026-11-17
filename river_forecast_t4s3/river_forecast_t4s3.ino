// River forecast display - LilyGo T4-S3 2.41" AMOLED (ESP32-S3, Wi-Fi).
//
// Shows the highest NOAA-predicted stage for the San Joaquin River at Vernalis
// (gauge VNSC1) over the next 120 hours. The board fetches the forecast itself
// at 12 AM, 6 AM, 12 PM and 6 PM Pacific, plus at boot and on a tap or BOOT press.
// Wi-Fi is switched off between fetches.
//
// Wi-Fi setup on the touchscreen: opens by itself when no network is saved; a tap
// opens it when the saved network can't be joined; hold the screen (or BOOT) for
// 3 s to open it any time. Pick a network, type the password, JOIN.
//
// Forecast logic (src/core) is shared, unchanged, with the RP2350 prototype.
// Serial console (115200, newline):
//   WIFI|<ssid>|<password>   save network (NVS) and fetch now
//   WIFI?                    show saved SSID / connection state
//   FETCH                    fetch now
//   DEMO | SELFTEST | STATUS | PING | TIME|<unix> | FC|... (see src/core/app.h)
//
// Build: see README.md (arduino-cli), or Arduino IDE with
//   Board ESP32S3 Dev Module, USB CDC On Boot: Enabled, Flash 16MB,
//   Partition "16M Flash (3MB APP/9.9MB FATFS)", PSRAM: OPI PSRAM.

#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "Board.h"
#include "NoaaFetch.h"
#include "RiverUi.h"
#include "WifiSetup.h"
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

static Board board;
static Preferences prefs;
static WifiSetup wifi_setup;
static river_model_t model;
static fc_point_t fetch_pts[FC_MAX_POINTS];

static int64_t next_fetch_ms = 0;      // monotonic ms; 0 = nothing scheduled yet
static int retries = 0;
static int64_t last_manual_ms = -MANUAL_MIN_GAP_MS;
static bool fetch_requested = false;
static bool wifi_problem = false;      // not configured, or the saved network can't be joined

// ---------------------------------------------------------------- clock / io
static int64_t mono_ms() { return esp_timer_get_time() / 1000; }

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

// Success: next slot. Failure: retry every 10 min, up to 3 times, then wait for the next slot.
static void schedule_after(bool ok) {
  const int64_t now = clock_now();
  if (!ok && retries < MAX_RETRIES) {
    retries++;
    next_fetch_ms = mono_ms() + RETRY_DELAY_MS;
  } else {
    retries = 0;
    const int64_t s = now ? secs_to_next_slot(now) : 6 * 3600;   // no clock yet: just wait 6 h
    next_fetch_ms = mono_ms() + s * 1000;
  }
  const int64_t in_s = (next_fetch_ms - mono_ms()) / 1000;
  Serial.printf("[SCHED] next fetch in %lldh%02lldm\n", (long long)(in_s / 3600), (long long)((in_s % 3600) / 60));
}

// ---------------------------------------------------------------- display
static UiExtras extras;
static uint32_t drawn_seq = UINT32_MAX;
static int64_t next_ui_ms = 0;

static void update_extras() {
  board.battery(extras.batt_pct, extras.charging, extras.usb);
  const int64_t now = clock_now();
  static const int8_t SX[9] = {0, 3, 3, 0, -3, -3, -3, 0, 3}, SY[9] = {0, 0, 3, 3, 3, 0, -3, -3, -3};
  const int k = now ? (int)((now / SHIFT_PERIOD_S) % 9) : 0;
  extras.shift_x = SX[k];
  extras.shift_y = SY[k];
  extras.hint = wifi_problem ? "Tap to choose a Wi-Fi network" : nullptr;
  if (now) {
    pt_tm_t l;
    pt_to_pacific(now, &l);
    board.setBrightness(l.hour >= 22 || l.hour < 6 ? BRIGHT_NIGHT : BRIGHT_DAY);
  }
}

static void render(const char* activity = nullptr) {
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
static bool wifi_credentials(String& ssid, String& pass) {
  prefs.begin("river", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
  if (ssid.length()) return true;
  ssid = WIFI_SSID;
  pass = WIFI_PASSWORD;
  return ssid.length() > 0;
}

static void save_credentials(const String& ssid, const String& pass) {
  prefs.begin("river", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

static bool wifi_join(const String& ssid, const String& pass, char* err, size_t n) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
  const int64_t t0 = mono_ms();
  while (WiFi.status() != WL_CONNECTED && mono_ms() - t0 < WIFI_TIMEOUT_MS) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(err, n, "Wi-Fi: can't join \"%s\"", ssid.c_str());
    return false;
  }
  Serial.printf("[WIFI] connected to %s, IP %s, RSSI %d\n", ssid.c_str(), WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  return true;
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
    String ssid, pass;
    if (!wifi_credentials(ssid, pass)) {
      snprintf(err, sizeof err, "No Wi-Fi network set up");
      wifi_problem = true;
    } else {
      render("CONNECTING TO WI-FI...");
      joined = wifi_join(ssid, pass, err, sizeof err);
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
    }
  }
  wifi_off();
  if (!ok) {
    Serial.printf("[FETCH] failed: %s\n", err);
    app_report_error(&model, err);
  }
  schedule_after(ok);
  render();
}

static void open_wifi_setup(const String& note = "") {
  String s, p;
  wifi_setup.open(wifi_credentials(s, p), note);
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
    save_credentials(wifi_setup.ssid(), wifi_setup.password());
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
    String ssid, pass;
    bool have = wifi_credentials(ssid, pass);
    Serial.printf("WIFI|%s|%s\n", have ? ssid.c_str() : "(not set)",
                  WiFi.status() == WL_CONNECTED ? "connected" : "off");
    return;
  }
  // WIFI|ssid|password  (password may itself contain '|')
  const int a = line.indexOf('|');
  const int b = line.indexOf('|', a + 1);
  if (a < 0 || b < 0 || b == a + 1) {
    Serial.println("NAK|usage: WIFI|<ssid>|<password>");
    return;
  }
  save_credentials(line.substring(a + 1, b), line.substring(b + 1));
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
    if (line.startsWith("WIFI")) {
      handle_wifi_command(line);
    } else if (line == "FETCH") {
      fetch_requested = true;
    } else if (line == "SETUP") {
      open_wifi_setup();
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
  Serial.begin(115200);
  delay(300);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  app_init(&model, &io);
  if (!board.begin()) Serial.println("[BOOT] display failed; continuing headless");
  Serial.println("READY|river-forecast|" APP_FW_VERSION "|t4-s3");

  String ssid, pass;
  if (wifi_credentials(ssid, pass)) {
    render();
    fetch_requested = true;   // first fetch right away
  } else {
    wifi_problem = true;
    open_wifi_setup();
  }
}

void loop() {
  poll_serial();
  poll_input();

  if (wifi_setup.active()) {
    if (wifi_setup.needsScan()) {
      render();              // shows "Scanning..."
      wifi_setup.scan();
    }
    if (wifi_setup.dirty()) render();
    delay(10);
    return;                  // no scheduled fetches while the user is in setup
  }

  if (fetch_requested || (next_fetch_ms && mono_ms() >= next_fetch_ms)) {
    fetch_requested = false;
    do_fetch();
  }

  if (model.seq != drawn_seq || mono_ms() >= next_ui_ms) render();
  delay(10);
}
