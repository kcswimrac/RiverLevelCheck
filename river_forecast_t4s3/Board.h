#pragma once
// LilyGo T4-S3 2.41" AMOLED (RM690B0 QSPI 450x600, CST226 touch, SY6970 PMU).
// Same bring-up as GILLIG_CAN_FW_V3/sketches/t4/DisplayAmoled.h: the LilyGo
// driver owns the panel, a TFT_eSPI instance only parents a full-screen
// 16-bit PSRAM sprite that is pushed in one QSPI transfer.
#include <LilyGo_AMOLED.h>
#include <TFT_eSPI.h>
#include <driver/gpio.h>

class Board {
 public:
  Board() : spr_(&tft_) {}

  bool begin() {
    gpio_hold_dis(PANEL_POWER_PIN);   // held low through deep sleep by sleep()
    if (!amoled_.beginAMOLED_241(/*disable_sd=*/true) && !amoled_.begin()) {
      Serial.println("[AMOLED] init failed");
      return false;
    }
    amoled_.setRotation(1);   // portrait 450x600
    spr_.setAttribute(PSRAM_ENABLE, true);
    spr_.setColorDepth(16);
    if (!spr_.createSprite(W, H)) {
      Serial.println("[AMOLED] sprite alloc failed");
      return false;
    }
    spr_.setSwapBytes(true);
    amoled_.setBrightness(brightness_);
    spr_.fillSprite(TFT_BLACK);
    flush();
    Serial.printf("[AMOLED] %dx%d up, touch %s\n", W, H, amoled_.hasTouch() ? "yes" : "no");
    return true;
  }

  TFT_eSprite& canvas() { return spr_; }
  void flush() { amoled_.pushColors(0, 0, W, H, (uint16_t*)spr_.getPointer()); }

  void setBrightness(uint8_t level) {
    if (level == brightness_) return;
    brightness_ = level;
    amoled_.setBrightness(level);
  }

  // True while a finger is on the panel; x/y in screen pixels (driver maps rotation).
  bool touchPoint(int& x, int& y) {
    int16_t tx, ty;
    if (!amoled_.hasTouch() || amoled_.getPoint(&tx, &ty) == 0) return false;
    x = tx;
    y = ty;
    return true;
  }

  // pct = -1 when no cell is detected (below 2.5 V).
  void battery(int& mv, int& pct, bool& charging, bool& vbus) {
    mv = amoled_.getBattVoltage();
    charging = amoled_.isCharging();
    vbus = amoled_.isVbusIn();
    if (mv < 2500) { pct = -1; return; }
    int p = (mv - 3300) * 100 / (4200 - 3300);
    pct = p < 0 ? 0 : (p > 100 ? 100 : p);
  }

  // Panel off for deep sleep: AMOLED sleep-in, touch controller to sleep, charger ADC
  // off, panel supply (GPIO 9) low and held low while the ESP32 sleeps.
  void sleep() {
    amoled_.setBrightness(0);
    amoled_.sleep(/*touchpad_sleep_enable=*/true);
    gpio_hold_en(PANEL_POWER_PIN);
    gpio_deep_sleep_hold_en();
  }

  static constexpr int W = 450, H = 600;
  static constexpr gpio_num_t PANEL_POWER_PIN = GPIO_NUM_9;   // BOARD_AMOLED_241.PMICEnPins

 private:
  LilyGo_Class amoled_;
  TFT_eSPI tft_;
  TFT_eSprite spr_;
  uint8_t brightness_ = 150;
};
