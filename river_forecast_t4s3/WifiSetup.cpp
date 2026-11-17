#include "WifiSetup.h"

#include <WiFi.h>

#include "src/fonts/fonts.h"

namespace {

constexpr int W = 450;
constexpr uint32_t COL_BG = 0x000000, COL_TEXT = 0xF2F4F7, COL_MUTED = 0x8A94A6, COL_DIM = 0x2A2F38,
                   COL_PANEL = 0x141922, COL_KEY = 0x1E2530, COL_ACCENT = 0x38BDF8, COL_BAD = 0xF2555A,
                   COL_OK = 0x34D399;

uint16_t c565(uint32_t rgb) { return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F); }

void text(TFT_eSprite& s, const uint8_t* font, const String& str, int x, int y, uint8_t datum, uint32_t fg,
          uint32_t bg) {
  s.loadFont(font);
  s.setTextDatum(datum);
  s.setTextColor(c565(fg), c565(bg));
  s.drawString(str, x, y);
  s.unloadFont();
}

// Shorten to fit max_w pixels, keeping the start ("My Netwo...") or the end ("...ssword").
String fit(TFT_eSprite& s, const uint8_t* font, String str, int max_w, bool keep_end) {
  s.loadFont(font);
  if (s.textWidth(str) > max_w) {
    while (str.length() > 1 && s.textWidth(keep_end ? "..." + str : str + "...") > max_w)
      str = keep_end ? str.substring(1) : str.substring(0, str.length() - 1);
    str = keep_end ? "..." + str : str + "...";
  }
  s.unloadFont();
  return str;
}

void button(TFT_eSprite& s, int x, int y, int w, int h, const char* label, uint32_t fg, uint32_t bg,
            const uint8_t* font = font_mont_20) {
  s.fillRoundRect(x, y, w, h, 10, c565(bg));
  text(s, font, label, x + w / 2, y + h / 2 + 1, MC_DATUM, fg, bg);
}

bool hit(int x, int y, int bx, int by, int bw, int bh) { return x >= bx && x < bx + bw && y >= by && y < by + bh; }

void bars(TFT_eSprite& s, int x, int y, int rssi, uint32_t bg) {
  const int level = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    const int h = 6 + i * 5;
    s.fillRect(x + i * 7, y + 22 - h, 5, h, c565(i < level ? COL_TEXT : COL_DIM));
  }
  (void)bg;
}

void padlock(TFT_eSprite& s, int x, int y, uint32_t col) {
  s.drawCircle(x + 7, y + 7, 5, c565(col));
  s.drawCircle(x + 7, y + 7, 4, c565(col));
  s.fillRect(x + 1, y + 9, 13, 11, c565(col));
}

// Button / key geometry
constexpr int LIST_Y = 108, ROW_H = 58;
constexpr int BTN_Y = 530, BTN_H = 52;
constexpr int KB_Y = 300, KEY_H = 54, KEY_GAP = 8, UNIT = 41, KEY_PITCH = 44;   // 10 x 44 = 440
constexpr int KB_X0 = (W - 10 * KEY_PITCH + (KEY_PITCH - UNIT)) / 2;
constexpr int FIELD_Y = 196, FIELD_H = 60;

const char* const ROWS[4][3] = {
    {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
    {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
    {"1234567890", "-_@#$%&*!", "()'\":;?"},
    {"[]{}<>^~`|", "\\/+=,.$%?", "\"'!?,.@"},
};

}  // namespace

void WifiSetup::open(bool can_cancel, const String& note) {
  active_ = true;
  can_cancel_ = can_cancel;
  page_ = Page::List;
  scan_pending_ = true;
  scanned_ = false;
  note_ = note;
  dirty_ = true;
}

void WifiSetup::scan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  const int n = WiFi.scanNetworks(false, false);
  n_nets_ = 0;
  // Strongest first, one row per SSID, hidden networks skipped.
  bool used[64] = {false};
  const int total = n < 0 ? 0 : (n > 64 ? 64 : n);
  while (n_nets_ < MAX_NETS) {
    int best = -1;
    for (int i = 0; i < total; i++)
      if (!used[i] && WiFi.SSID(i).length() && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
    if (best < 0) break;
    used[best] = true;
    bool dup = false;
    for (int j = 0; j < n_nets_; j++) dup |= nets_[j].ssid == WiFi.SSID(best);
    if (dup) continue;
    nets_[n_nets_++] = {WiFi.SSID(best), WiFi.RSSI(best), WiFi.encryptionType(best) == WIFI_AUTH_OPEN};
  }
  WiFi.scanDelete();
  Serial.printf("[SETUP] scan: %d networks, showing %d\n", n, n_nets_);
  scan_pending_ = false;
  scanned_ = true;
  dirty_ = true;
}

void WifiSetup::showJoining() {
  page_ = Page::Joining;
  dirty_ = true;
}

void WifiSetup::joinFailed(const String& why) {
  page_ = Page::Keyboard;
  field_ = Field::Password;
  note_ = why;
  layoutKeys();
  dirty_ = true;
}

bool WifiSetup::dirty() {
  if (pressed_ >= 0 && millis() > pressed_until_) {
    pressed_ = -1;
    dirty_ = true;
  }
  bool d = dirty_;
  dirty_ = false;
  return d && active_;
}

void WifiSetup::layoutKeys() {
  n_keys_ = 0;
  const int k = (int)keys_;
  auto add = [&](float ux, int row, float uw, const char* label, char ch, int code) {
    const int x = KB_X0 + (int)(ux * KEY_PITCH), y = KB_Y + row * (KEY_H + KEY_GAP);
    const int w = (int)(uw * KEY_PITCH) - (KEY_PITCH - UNIT);
    key_[n_keys_++] = {x, y, w, KEY_H, label, ch, code};
  };
  static char glyph[40][2];
  int g = 0;
  auto chars = [&](const char* row, float start, int r) {
    for (int i = 0; row[i]; i++) {
      glyph[g][0] = row[i];
      glyph[g][1] = 0;
      add(start + i, r, 1, glyph[g], row[i], K_CHAR);
      g++;
    }
  };
  chars(ROWS[k][0], 0, 0);
  chars(ROWS[k][1], 0.5f, 1);
  const bool sym = keys_ == Keys::Sym1 || keys_ == Keys::Sym2;
  add(0, 2, 1.5f, sym ? (keys_ == Keys::Sym1 ? "#+=" : "123") : (keys_ == Keys::Upper ? "abc" : "ABC"), 0,
      sym ? K_SYMPAGE : K_SHIFT);
  chars(ROWS[k][2], 1.5f, 2);
  add(8.5f, 2, 1.5f, "DEL", 0, K_DEL);
  add(0, 3, 2, sym ? "ABC" : "123", 0, K_MODE);
  add(2, 3, 5, "space", ' ', K_SPACE);
  add(7, 3, 3, field_ == Field::Ssid ? "NEXT" : "JOIN", 0, K_JOIN);
}

WifiSetup::Action WifiSetup::pressKey(const Key& k) {
  switch (k.code) {
    case K_CHAR:
    case K_SPACE:
      if (text_.length() < 63) text_ += k.ch;
      if (keys_ == Keys::Upper) { keys_ = Keys::Lower; layoutKeys(); }   // one-shot shift
      break;
    case K_DEL:
      if (text_.length()) text_.remove(text_.length() - 1);
      break;
    case K_SHIFT:
      keys_ = keys_ == Keys::Upper ? Keys::Lower : Keys::Upper;
      layoutKeys();
      break;
    case K_SYMPAGE:
      keys_ = keys_ == Keys::Sym1 ? Keys::Sym2 : Keys::Sym1;
      layoutKeys();
      break;
    case K_MODE:
      keys_ = (keys_ == Keys::Sym1 || keys_ == Keys::Sym2) ? Keys::Lower : Keys::Sym1;
      layoutKeys();
      break;
    case K_JOIN:
      if (field_ == Field::Ssid) {
        if (!text_.length()) return Action::None;
        ssid_ = text_;
        text_ = "";
        field_ = Field::Password;
        note_ = "";
        layoutKeys();
        return Action::None;
      }
      return Action::Join;   // empty password = open network
  }
  return Action::None;
}

WifiSetup::Action WifiSetup::onTouch(int x, int y) {
  if (!active_) return Action::None;
  dirty_ = true;
  if (page_ == Page::List) {
    if (scan_pending_) return Action::None;
    for (int i = 0; i < n_nets_; i++) {
      if (hit(x, y, 16, LIST_Y + i * ROW_H, W - 32, ROW_H - 6)) {
        ssid_ = nets_[i].ssid;
        text_ = "";
        note_ = "";
        if (nets_[i].open) return Action::Join;
        field_ = Field::Password;
        keys_ = Keys::Lower;
        page_ = Page::Keyboard;
        layoutKeys();
        return Action::None;
      }
    }
    const int bw = can_cancel_ ? 130 : 200, gap = 12;
    const int nb = can_cancel_ ? 3 : 2, x0 = (W - (nb * bw + (nb - 1) * gap)) / 2;
    if (hit(x, y, x0, BTN_Y, bw, BTN_H)) { scan_pending_ = true; return Action::None; }
    if (hit(x, y, x0 + bw + gap, BTN_Y, bw, BTN_H)) {
      ssid_ = text_ = note_ = "";
      field_ = Field::Ssid;
      keys_ = Keys::Lower;
      page_ = Page::Keyboard;
      layoutKeys();
      return Action::None;
    }
    if (can_cancel_ && hit(x, y, x0 + 2 * (bw + gap), BTN_Y, bw, BTN_H)) { active_ = false; return Action::Cancel; }
    return Action::None;
  }
  if (page_ == Page::Keyboard) {
    if (hit(x, y, 0, 0, 130, 80)) {   // BACK
      page_ = Page::List;
      note_ = "";
      return Action::None;
    }
    if (hit(x, y, W - 110, FIELD_Y, 94, FIELD_H) && field_ == Field::Password) {
      show_text_ = !show_text_;
      return Action::None;
    }
    for (int i = 0; i < n_keys_; i++) {
      const Key& k = key_[i];
      if (hit(x, y, k.x - 2, k.y - 3, k.w + 4, k.h + 6)) {
        pressed_ = i;
        pressed_until_ = millis() + 150;
        return pressKey(k);
      }
    }
  }
  return Action::None;
}

void WifiSetup::render(TFT_eSprite& s) {
  s.fillSprite(c565(COL_BG));

  if (page_ == Page::List) {
    text(s, font_mont_32, "Wi-Fi network", W / 2, 24, TC_DATUM, COL_TEXT, COL_BG);
    String sub = scan_pending_ ? String("Scanning...") : note_.length() ? note_ : String("Tap the network to use");
    text(s, font_mont_20, fit(s, font_mont_20, sub, W - 40, false), W / 2, 70, TC_DATUM,
         note_.length() && !scan_pending_ ? COL_BAD : COL_MUTED, COL_BG);
    if (!scan_pending_ && scanned_ && n_nets_ == 0)
      text(s, font_mont_26, "No networks found", W / 2, 300, MC_DATUM, COL_MUTED, COL_BG);
    for (int i = 0; i < n_nets_ && !scan_pending_; i++) {
      const int y = LIST_Y + i * ROW_H;
      s.fillRoundRect(16, y, W - 32, ROW_H - 6, 10, c565(COL_PANEL));
      text(s, font_mont_26, fit(s, font_mont_26, nets_[i].ssid, W - 140, false), 32, y + (ROW_H - 6) / 2 + 1,
           ML_DATUM, COL_TEXT, COL_PANEL);
      if (!nets_[i].open) padlock(s, W - 104, y + 15, COL_MUTED);
      bars(s, W - 72, y + 14, nets_[i].rssi, COL_PANEL);
    }
    const int bw = can_cancel_ ? 130 : 200, gap = 12;
    const int nb = can_cancel_ ? 3 : 2, x0 = (W - (nb * bw + (nb - 1) * gap)) / 2;
    button(s, x0, BTN_Y, bw, BTN_H, "RESCAN", COL_TEXT, COL_KEY);
    button(s, x0 + bw + gap, BTN_Y, bw, BTN_H, "OTHER...", COL_TEXT, COL_KEY);
    if (can_cancel_) button(s, x0 + 2 * (bw + gap), BTN_Y, bw, BTN_H, "CANCEL", COL_MUTED, COL_KEY);
    return;
  }

  if (page_ == Page::Joining) {
    text(s, font_mont_32, "Joining", W / 2, 230, MC_DATUM, COL_TEXT, COL_BG);
    text(s, font_mont_26, fit(s, font_mont_26, ssid_, W - 40, false), W / 2, 280, MC_DATUM, COL_ACCENT, COL_BG);
    text(s, font_mont_20, "This can take up to 20 seconds", W / 2, 330, MC_DATUM, COL_MUTED, COL_BG);
    return;
  }

  // Keyboard page
  text(s, font_mont_26, "< BACK", 20, 26, TL_DATUM, COL_ACCENT, COL_BG);
  const bool pw = field_ == Field::Password;
  text(s, font_mont_32, pw ? "Password" : "Network name", W / 2, 76, TC_DATUM, COL_TEXT, COL_BG);
  if (pw) text(s, font_mont_20, fit(s, font_mont_20, "for " + ssid_, W - 40, false), W / 2, 118, TC_DATUM,
               COL_MUTED, COL_BG);
  if (note_.length()) text(s, font_mont_20, fit(s, font_mont_20, note_, W - 40, false), W / 2, 150, TC_DATUM,
                           COL_BAD, COL_BG);

  s.fillRoundRect(16, FIELD_Y, W - 32, FIELD_H, 10, c565(COL_PANEL));
  s.drawRoundRect(16, FIELD_Y, W - 32, FIELD_H, 10, c565(COL_ACCENT));
  String shown = text_;
  if (pw && !show_text_) { shown = ""; for (unsigned i = 0; i < text_.length(); i++) shown += '*'; }
  const int field_w = pw ? W - 32 - 130 : W - 32 - 40;
  shown = fit(s, font_mont_26, shown, field_w, true);
  s.loadFont(font_mont_26);
  const int tw = s.textWidth(shown);
  s.unloadFont();
  text(s, font_mont_26, shown, 32, FIELD_Y + FIELD_H / 2 + 1, ML_DATUM, COL_TEXT, COL_PANEL);
  s.fillRect(34 + tw, FIELD_Y + 14, 3, FIELD_H - 28, c565(COL_ACCENT));   // cursor
  if (pw) text(s, font_mont_20, show_text_ ? "HIDE" : "SHOW", W - 64, FIELD_Y + FIELD_H / 2 + 1, MC_DATUM,
               COL_ACCENT, COL_PANEL);

  for (int i = 0; i < n_keys_; i++) {
    const Key& k = key_[i];
    const bool special = k.code != K_CHAR && k.code != K_SPACE;
    uint32_t bg = i == pressed_ ? COL_ACCENT : (k.code == K_JOIN ? 0x0B4F6C : (special ? COL_PANEL : COL_KEY));
    uint32_t fg = i == pressed_ ? COL_BG : COL_TEXT;
    if (k.code == K_SHIFT && keys_ == Keys::Upper) { bg = COL_ACCENT; fg = COL_BG; }
    button(s, k.x, k.y, k.w, k.h, k.label, fg, bg, k.code == K_CHAR ? font_mont_26 : font_mont_20);
  }
}
