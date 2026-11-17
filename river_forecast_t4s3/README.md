# River forecast display: LilyGo T4-S3 2.41" AMOLED

Shows the **highest NOAA-predicted river stage over the next 120 hours** for the
San Joaquin River at Vernalis (gauge **VNSC1**). The ESP32-S3 fetches the forecast itself over Wi-Fi.

## Behaviour

- Fetches at boot, then at **12 AM, 6 AM, 12 PM and 6 PM Pacific** (DST-aware), plus on a screen tap
  or a BOOT-button press (at most one manual fetch every 20 s).
- On failure it retries every 10 min, up to 3 times, then waits for the next slot. The last good
  value stays on screen, marked **UPDATE FAILED · SHOWING LAST DATA**.
- Wi-Fi is turned off between fetches. The display dims from 10 PM to 6 AM and shifts its layout
  by a few pixels every 10 min to protect the AMOLED from burn-in.
- Data: `GET https://api.water.noaa.gov/nwps/v1/gauges/VNSC1/stageflow/forecast` over verified TLS.
  The firmware embeds Amazon Root CA 1 and Starfield Services Root G2 (`noaa_roots.h`, valid to 2037/38).
  It uses only `primary` (Stage, **ft**) and refuses any other units.

## What the screen means

| Element | Meaning |
|---|---|
| Big number | Highest predicted stage between now (minus 1 h) and now + 120 h |
| `NEXT 5 DAYS` | Full forecast: data reaches ≥ 119 h, no gaps over 6 h, NOAA issuance under 36 h old |
| amber `ONLY 113 OF 120 HRS FORECAST` | NOAA's forecast ends early. The value is the max of what exists, not a 5-day max |
| amber `STILL RISING AT … FORECAST END` | Short forecast whose last point is its highest: the real peak may be later |
| amber `FORECAST HAS GAPS` / `NOAA FORECAST ISSUED … HRS AGO` | Holes in the data, or a stale NOAA issuance |
| Chart | Hourly stage for the 120 h window, white dot = peak, hatched = hours with no forecast |
| Pill | `COMPLETE 5-DAY FORECAST` (green), `PARTIAL FORECAST` (amber), `STALE` (no good update for 7 h), `UPDATE FAILED`, `DEMO` |

## Wi-Fi setup

**On the touchscreen.** The setup screen opens by itself when no network is saved. When the saved
network can't be joined, the forecast screen says *Tap to choose a Wi-Fi network*. You can also hold a
finger on the screen, or hold BOOT, for 3 seconds at any time.

1. The list shows nearby networks, strongest first, with a padlock for secured ones and signal bars.
   Use **RESCAN** to refresh, **OTHER...** for a hidden network, and **CANCEL** to keep the saved network.
2. Tap a network and type the password on the keyboard: **ABC/abc** is shift, **123** and **#+=** switch to
   symbols, and **SHOW/HIDE** reveals what you typed.
3. **JOIN** tests the network. On success it is saved to flash (NVS) and a NOAA fetch starts. On failure you
   return to the keyboard with your text kept.

**Alternatives.** Send `WIFI|YourNetwork|YourPassword` from a serial monitor (115200 baud, newline line endings),
or fill in `secrets.h` (git-ignored, see `secrets_example.h`). `WIFI?` shows the saved SSID and `SETUP` opens
the setup screen.

## Serial commands

`FETCH`, `STATUS`, `SELFTEST` (runs 20 forecast/DST test vectors on the board), `DEMO`, `PING`,
`TIME|<unix>`, and `FC|BEGIN/PT/END` to inject a saved forecast (see `src/core/app.h`).

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
  pure C, shared with the RP2350 prototype (`Documents/Claude/river-forecast-rp2350`), and ready for e-paper later.
- `NoaaFetch.cpp` handles HTTPS and filtered JSON parsing. `RiverUi.cpp` draws the screen. `Board.h` wraps the LilyGo panel, touch and PMU.
- `tools/make_vlw.py` generates the anti-aliased Montserrat fonts in `src/fonts/` (OFL font).

## Not done yet (Phase 5/6)

Deep sleep between fetches. An AMOLED goes dark when the chip sleeps, so this waits for the e-paper or
split-flap display; the model is small enough to keep in RTC memory. Measuring real current draw.
