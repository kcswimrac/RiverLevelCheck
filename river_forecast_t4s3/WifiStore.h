#pragma once
// Saved Wi-Fi networks: up to MAX in NVS (namespace "river"), most recently used
// first. Adding a sixth drops the one used longest ago. A network compiled in via
// secrets.h is used only while nothing is saved.
#include <Arduino.h>

class WifiStore {
 public:
  static constexpr int MAX = 5;

  void begin(const char* fallback_ssid, const char* fallback_pass);   // load (and migrate old single entry)
  int count() const { return n_ ? n_ : (fallback_ssid_.length() ? 1 : 0); }
  const String& ssid(int i) const { return n_ ? ssid_[i] : fallback_ssid_; }
  const String& pass(int i) const { return n_ ? pass_[i] : fallback_pass_; }
  int find(const String& ssid) const;                    // index or -1
  void remember(const String& ssid, const String& pass); // add or update, move to front, save
  void used(const String& ssid);                         // move to front (saves only if the order changes)
  bool forget(const String& ssid);

 private:
  void save();
  int n_ = 0;
  String ssid_[MAX], pass_[MAX];
  String fallback_ssid_, fallback_pass_;
};
