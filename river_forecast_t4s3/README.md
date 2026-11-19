# River forecast display: LilyGo T4-S3 2.41" AMOLED

Shows the **current river height** and the **highest NOAA-predicted stage over the next 120 hours**
for the San Joaquin River at Vernalis (gauge **VNSC1**), with a chart of the last 24 hours and the
forecast. The ESP32-S3 fetches everything itself over Wi-Fi.

## Behaviour

- Fetches at boot, then at **12 AM, 6 AM, 12 PM and 6 PM Pacific** (DST-aware), plus on a screen tap
  or a BOOT-button press (at most one manual fetch every 20 s).
- On failure it retries every 10 min, up to 3 times, then waits for the next slot. The last good
  value stays on screen, marked **UPDATE FAILED · SHOWING LAST DATA**.
- Wi-Fi is turned off between fetches. The display dims from 10 PM to 6 AM and shifts its layout
  by a few pixels every 10 min to protect the AMOLED from burn-in.
- Data, over verified TLS (Amazon Root CA 1 and Starfield Services Root G2 are embedded in `noaa_roots.h`,
  valid to 2037/38):
  - `GET https://api.water.noaa.gov/nwps/v1/gauges/VNSC1/stageflow/forecast` gives the forecast and peak.
  - `GET .../stageflow/observed` gives the gauge readings. It returns about 30 days at 15-min intervals
    (~430 KB); the firmware scans the stream and keeps only the last 25 h. If it fails, the forecast is
    still shown and NOW reads `--.--`.
  - Only `primary` (Stage, **ft**) is used, and any other units are refused.

## Battery and sleep

**The screen stays on**, on USB and on battery. On battery an always-on AMOLED runs the cell down in
hours rather than days, so plan on USB power for a permanent display.

Deep sleep is built in but switched off (`SLEEP_ON_BATTERY = false` in the sketch). With it on:

| Power | What it does |
|---|---|
| **USB** | Always awake. |
| **Battery** | Shows the forecast for **60 s** after the last touch (3 min in Wi-Fi setup), then turns the panel off and **deep-sleeps** until the next fetch. |
| Timer wake (battery) | Wakes at the 12/6 slot, fetches with the screen **off**, and goes straight back to sleep. |
| **BOOT button** | Wakes the board and shows the saved forecast at once. The press only wakes it; a later tap refreshes. |
| Battery under 3.4 V | Scheduled fetches are skipped (Wi-Fi peaks could brown it out). The screen says *Battery low - charge to keep updating*. |

While asleep the panel supply (GPIO 9) is held off, the touch controller sleeps and the charger's ADC
is off. Only the BOOT button wakes it: touch can't, because the touch chip is asleep. The forecast,
schedule and Wi-Fi status live in RTC memory, so a wake shows the last result without fetching. Power
loss, the RST button or a reflash clears them, and the board then fetches again. Saved networks are in
flash and are never lost this way.

The RTC runs on the ESP32's internal oscillator and can drift a few minutes over 6 h. A timer wake within
15 min of a slot fetches, and the next fetch is always at least 30 min away, so drift never causes a double fetch.

### Measuring battery life

`POWER` on serial prints a summary and one `POWERLOG` line per awake period. Each line gives the time,
the battery mV before sleep, the seconds awake, the wake reason, and USB or battery. After at least 6 h on
battery a `DRAIN|… mV/day` estimate appears; leave the board unplugged for a few days, then plug in and
send `POWER` before anything resets it.

```
POWER|BATTERY|mv=3987|pct=76|charging=0|wake=button|boots=1|timer=8|button=3|awake=412s|asleep=172800s|duty=0.24%|...
POWERLOG|1795262400|3991|9|timer|batt|fetch-ok
DRAIN|11.5 mV/day over 48.0 h (3998 -> 3975 mV)
```

The charger cannot measure discharge current. For a real number, put a USB power meter or a µA meter
in series with the battery. Unmeasured ballpark figures for this board: 100–150 mA while awake with the screen on, 80–120 mA during a
Wi-Fi fetch (about 5–10 s), and well under 1 mA asleep (ESP32-S3 deep sleep is about 10 µA; the
rest is the charger and regulator quiescent draw).

## What the screen means

| Element | Meaning |
|---|---|
| **NOW** | Latest gauge reading and its time. Grey with an amber time when it is over 3 h old; `--.--` when no reading in the last 12 h |
| **EXPECTED PEAK** | Highest predicted stage between now (minus 1 h) and now + 120 h, and when it happens (`now` if that is within the hour). NOAA's forecast only; a river already above its forecast can read higher under NOW |
| `NEXT 5 DAYS` | Full forecast: data reaches ≥ 119 h, no gaps over 6 h, NOAA issuance under 36 h old |
| amber `ONLY 113 OF 120 HRS FORECAST` | NOAA's forecast ends early. The value is the max of what exists, not a 5-day max |
| amber `STILL RISING AT … FORECAST END` | Short forecast whose last point is its highest: the real peak may be later |
| amber `FORECAST HAS GAPS` / `NOAA FORECAST ISSUED … HRS AGO` | Holes in the data, or a stale NOAA issuance |
| Chart | Grey: observed, the last 24 h. Colour: forecast. The x axis runs from the first reading to the last forecast hour, so a short forecast fills the width instead of leaving blank space. A dashed line marks NOW; dots mark the latest reading and the peak; day names sit under each day |
| Pill | `COMPLETE 5-DAY FORECAST` (green), `PARTIAL FORECAST` (amber), `STALE` (no good update for 7 h), `UPDATE FAILED`, `DEMO` |

## Wi-Fi setup

**Several networks.** Up to **5** networks are saved, for example home, office and a phone hotspot. Each
fetch scans and joins the strongest saved network in range. If none shows up in the scan, it tries each saved
one briefly in case the network is hidden. Saving a sixth drops the one used longest ago.

**On the touchscreen.** The setup screen opens by itself when nothing is saved. When no saved network can
be joined, the forecast screen says *Tap to choose a Wi-Fi network*. You can also hold a finger on the
screen, or hold BOOT, for 3 seconds at any time.

1. The list shows nearby networks, strongest first, with a padlock for secured ones, signal bars, and
   **SAVED** on networks it already knows. Use **RESCAN** to refresh, **OTHER...** for a hidden network,
   and **CANCEL** to leave.
2. Tap a **SAVED** network to join it at once with its stored password. For a new one, type the password:
   **ABC/abc** is shift, **123** and **#+=** switch to symbols, and **SHOW/HIDE** reveals what you typed.
3. **JOIN** tests the network. On success it is added to the saved list in flash (NVS) and a NOAA fetch
   starts. On failure you return to the keyboard with your text kept. If a saved password has stopped
   working, the keyboard opens empty so you can type the new one.

**Alternatives.** On a serial monitor (115200 baud, newline line endings): `WIFI|YourNetwork|YourPassword`
adds a network, `WIFI?` lists the saved ones (names only), `WIFI-|YourNetwork` forgets one, and `SETUP`
opens the setup screen. You can also fill in `secrets.h` (git-ignored, see `secrets_example.h`); it is
used only while nothing is saved. A network saved by firmware 0.2/0.3 carries over automatically.

## Serial commands

`FETCH`, `STATUS`, `SELFTEST` (runs 20 forecast/DST test vectors on the board), `DEMO` (sample data; `DEMO|22.20|28.75` picks the NOW and peak values, for checking the layout), `PING`,
`TIME|<unix>`, and `FC|BEGIN/PT/END` to inject a saved forecast (see `src/core/app.h`).

Power: `POWER` (stats and battery log), `SLEEP` (sleep now until the next fetch), `SLEEP|<secs>` (sleep
for a set time, for testing) and `PWRMODE|BATTERY` (act as if unplugged until the next wake, so the 60 s
auto-sleep can be tried on USB; `PWRMODE|AUTO` undoes it). The USB serial port disappears while the
board sleeps.

## Build and flash

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbn = "esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi"
& $cli compile --fqbn $fqbn --build-path build .
& $cli upload  --fqbn $fqbn --build-path build -p COM8 .
```

Libraries (already in `Documents/Arduino/libraries`): LilyGo-AMOLED-Series 1.2.4, TFT_eSPI 2.5.43
(with `SMOOTH_FONT`), ArduinoJson 7.4.3, XPowersLib, SensorLib. ESP32 core 3.3.11.

## Checking the board against NOAA

`python -I tools/check_noaa.py` fetches the same forecast on the PC with a Python copy of
`src/core/forecast.c` and prints the `RESULT|…` line the board should report (`STATUS` on serial).

## Layout

- `src/core/` holds the forecast window, peak and partial logic, Pacific time and the protocol. It is
  pure C, shared with the RP2350 prototype (`../rp2350`), and ready for e-paper later.
- `NoaaFetch.cpp` handles HTTPS and filtered JSON parsing. `RiverUi.cpp` draws the screen. `WifiSetup.cpp`
  is the on-screen network picker and keyboard, and `WifiStore.cpp` keeps the saved networks. `Board.h` wraps the LilyGo panel, touch, PMU and panel
  power. `Power.cpp` handles wake reasons, deep sleep and the battery log.
- `tools/make_vlw.py` generates the anti-aliased Montserrat fonts in `src/fonts/` (OFL font).

## Not done yet

- Phase 5: a measured current figure (see *Measuring battery life*) and, optionally, touch-to-wake. That
  would keep the touch chip awake in sleep and cost a few mA.
- Phase 6: an e-paper or split-flap display that keeps the forecast visible while asleep, and an enclosure.
