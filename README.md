# RiverLevelCheck

A small display that shows the **highest predicted river height over the next five days** for the
San Joaquin River at Vernalis, California (NOAA gauge **VNSC1**). It updates itself at 12 AM, 6 AM,
12 PM and 6 PM Pacific.

It looks ahead 120 hours instead of showing the current stage. The display separates a complete
five-day forecast from a shorter, gappy or stale one: NOAA's VNSC1 forecast often ends a few hours
short of 120, and the device shows **PARTIAL** rather than presenting it as a five-day maximum.

| Folder | Hardware | Status |
|---|---|---|
| [`river_forecast_t4s3/`](river_forecast_t4s3/) | LilyGo T4-S3 2.41" AMOLED (ESP32-S3, Wi-Fi) | **Main build.** Fetches NOAA itself over Wi-Fi, with on-screen Wi-Fi setup |
| [`rp2350/`](rp2350/) | Waveshare RP2350-Touch-AMOLED-1.75 (no Wi-Fi) | Phase 1–3 prototype, fed over USB by `rp2350/tools/noaa_usb_feed.py` |

Both builds compile the same pure-C core, [`river_forecast_t4s3/src/core/`](river_forecast_t4s3/src/core/):

- `forecast.c` handles the 120 h window, the peak (earliest time wins ties) and the FULL / PARTIAL / NONE
  status, with flags for a short horizon, gaps over 6 h, a NOAA issuance older than 36 h, and "still rising at forecast end".
- `pacific_time.c` converts UTC to America/Los_Angeles with US DST rules and needs no tz database.
- `app.c` holds the display model and the serial line protocol, used for testing and for the USB feed.
- `selftest.c` holds 20 test vectors. Send `SELFTEST` over serial to run them on either board.

`river_forecast_t4s3/tools/check_noaa.py` is a Python copy of the forecast logic, used to check a
board's `RESULT` line against live NOAA data.

## Data source

`GET https://api.water.noaa.gov/nwps/v1/gauges/VNSC1/stageflow/forecast`. Only `primary` (Stage, ft)
is used, and the firmware refuses any other units.

**This is a display aid, not a flood warning or life-safety system.** Official forecasts and warnings:
https://water.noaa.gov/gauges/VNSC1

## Roadmap

1. ~~Display bring-up (RP2350)~~
2. ~~Forecast data integration (USB-fed)~~
3. ~~River forecast interface~~
4. ~~Standalone Wi-Fi (T4-S3)~~
5. Battery optimisation: deep sleep between the four daily fetches and measured current draw
6. Low-power e-paper or split-flap display, and an enclosure

## Cloning

The RP2350 build uses LVGL v8.1.0 as a submodule:

```
git clone --recurse-submodules https://github.com/kcswimrac/RiverLevelCheck
```

See [NOTICE.md](NOTICE.md) for font, certificate and data credits.
