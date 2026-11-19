#include "WifiStore.h"

#include <Preferences.h>

namespace {
const char* NS = "river";
String key(char kind, int i) { return String(kind) + String(i); }   // "s0", "p0", ...
}  // namespace

void WifiStore::begin(const char* fallback_ssid, const char* fallback_pass) {
  fallback_ssid_ = fallback_ssid ? fallback_ssid : "";
  fallback_pass_ = fallback_pass ? fallback_pass : "";
  Preferences p;
  p.begin(NS, false);
  n_ = p.getUChar("wn", 0);
  if (n_ > MAX) n_ = MAX;
  for (int i = 0; i < n_; i++) {
    ssid_[i] = p.getString(key('s', i).c_str(), "");
    pass_[i] = p.getString(key('p', i).c_str(), "");
  }
  // Firmware 0.2/0.3 kept one network under "ssid"/"pass": migrate it.
  if (n_ == 0 && p.isKey("ssid")) {
    const String s = p.getString("ssid", ""), pw = p.getString("pass", "");
    p.remove("ssid");
    p.remove("pass");
    p.end();
    if (s.length()) remember(s, pw);
    return;
  }
  p.end();
}

int WifiStore::find(const String& ssid) const {
  for (int i = 0; i < n_; i++)
    if (ssid_[i] == ssid) return i;
  return -1;
}

void WifiStore::remember(const String& ssid_in, const String& pass_in) {
  const String ssid = ssid_in, pass = pass_in;   // copies: callers may pass our own entries
  int i = find(ssid);
  if (i < 0) i = n_ < MAX ? n_++ : MAX - 1;   // full: overwrite the least recently used
  for (; i > 0; i--) {
    ssid_[i] = ssid_[i - 1];
    pass_[i] = pass_[i - 1];
  }
  ssid_[0] = ssid;
  pass_[0] = pass;
  save();
}

void WifiStore::used(const String& ssid) {
  const int i = find(ssid);
  if (i <= 0) return;   // unknown, or already first
  const String pw = pass_[i];
  remember(ssid, pw);
}

bool WifiStore::forget(const String& ssid) {
  const int i = find(ssid);
  if (i < 0) return false;
  for (int j = i; j < n_ - 1; j++) {
    ssid_[j] = ssid_[j + 1];
    pass_[j] = pass_[j + 1];
  }
  n_--;
  ssid_[n_] = pass_[n_] = "";
  save();
  return true;
}

void WifiStore::save() {
  Preferences p;
  p.begin(NS, false);
  for (int i = 0; i < MAX; i++) {
    if (i < n_) {
      p.putString(key('s', i).c_str(), ssid_[i]);
      p.putString(key('p', i).c_str(), pass_[i]);
    } else {
      p.remove(key('s', i).c_str());
      p.remove(key('p', i).c_str());
    }
  }
  p.putUChar("wn", (uint8_t)n_);
  p.end();
}
