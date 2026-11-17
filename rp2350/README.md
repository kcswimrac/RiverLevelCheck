# RP2350 prototype: Waveshare RP2350-Touch-AMOLED-1.75

The Phase 1–3 development build on the 466x466 round AMOLED. The board has **no Wi-Fi**: a PC runs
`tools/noaa_usb_feed.py`, which fetches NOAA and streams the raw forecast points over USB. The board
itself finds the 120-hour peak and decides FULL / PARTIAL / NONE, using the shared core in
`../river_forecast_t4s3/src/core`.

The screen shows the max stage (128 px digits), a `NEXT 5 DAYS` / `ONLY 113 OF 120 HRS FORECAST` line,
the peak time, the update time and a status pill. The outer ring shows how much of the 120 h window the
forecast covers. Tap the screen to request a refresh.

## Build and flash

Needs the Raspberry Pi Pico VS Code extension's toolchain (Pico SDK 2.2.0, GCC 14.2, Ninja), and the LVGL
submodule (`git submodule update --init`).

```bash
cmake -G Ninja -S . -B build
ninja -C build
picotool load -x build/river-forecast.uf2      # board in BOOTSEL mode
```

Or drag `build/river-forecast.uf2` onto the RP2350 drive while the board is in BOOTSEL mode.

## Run the USB feed

```bash
python -m pip install pyserial
python tools/noaa_usb_feed.py --port COM9 --selftest   # board runs its 20 test vectors
python tools/noaa_usb_feed.py --port COM9 --once       # fetch NOAA, send, check RESULT vs reference
python tools/noaa_usb_feed.py --port COM9 --watch      # 00/06/12/18 Pacific + on screen tap
```

The PC's clock is sent with each update (`TIME|<unix>`). The on-board PCF85063 RTC keeps it across
resets; the driver uses year base 2000 so the chip's leap-year rule stays correct.

## Layout

- `main.c` brings up the board (DEV_Module_Init, QSPI PIO, CO5300 AMOLED, CST9217 touch), then runs LVGL and the USB line reader.
- `src/ui_river.c` is the LVGL screen. `src/clock.c` handles the RTC and wall clock. `src/fonts/font_stage_128.c` holds the digits, made by `tools/make_font.py`.
- `lib/Config`, `lib/QSPI_PIO`, `lib/AMOLED`, `lib/Touch` are the Waveshare board support package, from the board's
  example repository.
- `lib/lvgl` is the LVGL v8.1.0 submodule; `lib/lvgl_build` builds it as the `LVGL` target.
