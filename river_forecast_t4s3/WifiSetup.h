#pragma once
// On-screen Wi-Fi setup for the 450x600 touch panel:
//   LIST      scanned networks (strongest first, saved ones tagged) + RESCAN / OTHER / CANCEL
//   KEYBOARD  type a password (or, via OTHER, a hidden network's name first)
//   JOINING   shown while the main sketch tries the credentials
// The sketch owns the radio for joining; this class only scans and collects input.
#include <Arduino.h>
#include <TFT_eSPI.h>

#include "WifiStore.h"

class WifiSetup {
 public:
  enum class Action { None, Cancel, Join };

  // Saved networks join with one tap (no typing) and are tagged SAVED in the list.
  void setStore(const WifiStore* store) { store_ = store; }

  // Open the setup flow. can_cancel: a network is already saved, so offer CANCEL.
  void open(bool can_cancel, const String& note = "");
  void close() { active_ = false; }
  bool active() const { return active_; }

  // Blocking Wi-Fi scan when the list needs one (call after drawing "Scanning...").
  bool needsScan() const { return active_ && page_ == Page::List && scan_pending_; }
  void scan();

  Action onTouch(int x, int y);   // call on finger-down
  void render(TFT_eSprite& s);
  bool dirty();                   // true when the screen should be redrawn

  // Results for Action::Join.
  const String& ssid() const { return ssid_; }
  const String& password() const { return text_; }
  void showJoining();
  void joinFailed(const String& why);   // back to the keyboard, text kept

 private:
  enum class Page { List, Keyboard, Joining };
  enum class Field { Ssid, Password };
  enum class Keys { Lower, Upper, Sym1, Sym2 };
  struct Net { String ssid; int rssi; bool open; };
  struct Key { int x, y, w, h; const char* label; char ch; int code; };

  static constexpr int MAX_NETS = 7;
  enum { K_CHAR = 0, K_SHIFT, K_DEL, K_MODE, K_SPACE, K_JOIN, K_SYMPAGE };

  void layoutKeys();
  Action pressKey(const Key& k);

  const WifiStore* store_ = nullptr;
  bool tried_saved_ = false;   // the current Join used the stored password
  bool active_ = false, can_cancel_ = false, scan_pending_ = false, scanned_ = false;
  bool show_text_ = true, dirty_ = true;
  Page page_ = Page::List;
  Field field_ = Field::Password;
  Keys keys_ = Keys::Lower;
  Net nets_[MAX_NETS];
  int n_nets_ = 0;
  String ssid_, text_, note_;
  Key key_[40];
  int n_keys_ = 0, pressed_ = -1;
  uint32_t pressed_until_ = 0;
};
