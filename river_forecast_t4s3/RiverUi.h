#pragma once
#include <TFT_eSPI.h>

#include "src/core/app.h"

// Things the screen shows that are not part of the forecast model.
struct UiExtras {
  const char* activity = nullptr;   // e.g. "UPDATING FROM NOAA..." (overrides the pill)
  int batt_pct = -1;                // -1: no battery
  bool charging = false;
  bool usb = false;
  int shift_x = 0, shift_y = 0;     // AMOLED burn-in protection offset
  const char* hint = nullptr;       // replaces the footer line, e.g. "Tap to choose a Wi-Fi network"
};

// Draw the whole 450x600 frame into the sprite (caller flushes).
void ui_render(TFT_eSprite& s, const river_model_t& m, int64_t now, const UiExtras& x);
