#include "NoaaFetch.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "noaa_roots.h"
#include "src/core/pacific_time.h"

#define NOAA_URL_FMT "https://api.water.noaa.gov/nwps/v1/gauges/%s/stageflow/%s"

bool noaa_parse_time(const char* s, int64_t* out) {
  if (!s) return false;
  int y, mo, d, h, mi, se = 0, n = 0;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &se, &n) < 6 || n == 0) return false;
  const char* tz = s + n;
  if (*tz == '.') {                      // fractional seconds
    tz++;
    while (*tz >= '0' && *tz <= '9') tz++;
  }
  int64_t off = 0;
  if (*tz == 'Z' || *tz == 'z') {
    off = 0;
  } else if (*tz == '+' || *tz == '-') {
    int oh = 0, om = 0;
    if (sscanf(tz + 1, "%2d:%2d", &oh, &om) < 1) return false;
    off = (oh * 3600 + om * 60) * (*tz == '-' ? -1 : 1);
  } else {
    return false;                        // no timezone: refuse to guess
  }
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return false;
  *out = pt_utc_epoch(y, mo, d, h, mi, se) - off;
  return true;
}

static bool contains_ci(const char* hay, const char* needle) {
  if (!hay) return false;
  size_t n = strlen(needle);
  for (; *hay; hay++)
    if (strncasecmp(hay, needle, n) == 0) return true;
  return false;
}

// Verified-TLS GET of .../gauges/<gauge>/stageflow/<what>. On failure: message in err, false.
static bool noaa_get(HTTPClient& http, NetworkClientSecure& tls, const char* gauge, const char* what,
                     char* err, size_t err_len) {
  char url[128];
  snprintf(url, sizeof url, NOAA_URL_FMT, gauge, what);
  tls.setCACert(NOAA_ROOT_CAS);
  tls.setTimeout(15);   // seconds
  http.useHTTP10(true);   // no chunked encoding, so the stream can go straight to the parser
  http.setTimeout(15000);
  http.setUserAgent("river-forecast-display/0.4");
  if (!http.begin(tls, url)) {
    snprintf(err, err_len, "NOAA: bad URL");
    return false;
  }
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code <= 0) {
    snprintf(err, err_len, "NOAA connect failed (%s)", http.errorToString(code).c_str());
    http.end();
    return false;
  }
  if (code != 200) {
    snprintf(err, err_len, "NOAA HTTP %d", code);
    http.end();
    return false;
  }
  return true;
}

// Reads the whole response body in chunks, handing each to sink(data, len).
// Weak Wi-Fi can stall a TLS stream for seconds: give up only after 15 s with
// no data (or 2 min in total), and check the byte count against Content-Length.
template <typename Sink>
static bool read_body(HTTPClient& http, Sink&& sink, size_t* total, char* err, size_t err_len) {
  NetworkClient* stream = http.getStreamPtr();
  const int size = http.getSize();   // -1 if the server didn't say
  static uint8_t chunk[2048];
  size_t got = 0;
  const uint32_t t0 = millis();
  uint32_t last_rx = t0;
  while (size < 0 || got < (size_t)size) {
    int k = stream->available();
    if (k > 0) {
      k = stream->read(chunk, k > (int)sizeof chunk ? (int)sizeof chunk : k);
      if (k > 0) {
        got += k;
        last_rx = millis();
        if (!sink(chunk, k)) {
          snprintf(err, err_len, "NOAA: response too large");
          return false;
        }
        continue;
      }
    }
    if (!stream->connected() && stream->available() <= 0) break;   // server closed the connection
    if (millis() - last_rx > 15000 || millis() - t0 > 120000) {
      snprintf(err, err_len, "NOAA download stalled at %u bytes", (unsigned)got);
      return false;
    }
    delay(5);
  }
  *total = got;
  if (size > 0 && got < (size_t)size) {
    snprintf(err, err_len, "NOAA download cut off at %u of %d bytes", (unsigned)got, size);
    return false;
  }
  return true;
}

int noaa_fetch_forecast(const char* gauge, fc_point_t* pts, int max_pts, int64_t* issued_t,
                        char* err, size_t err_len) {
  NetworkClientSecure tls;
  HTTPClient http;
  if (!noaa_get(http, tls, gauge, "forecast", err, err_len)) return -1;

  JsonDocument filter;
  filter["issuedTime"] = true;
  filter["primaryName"] = true;
  filter["primaryUnits"] = true;
  filter["data"][0]["validTime"] = true;
  filter["data"][0]["primary"] = true;

  // The forecast is ~20 KB: download it all (tolerating stalls), then parse from memory.
  const int size = http.getSize();
  const size_t cap = size > 0 ? (size_t)size + 1 : 256 * 1024;
  char* body = (char*)ps_malloc(cap);
  if (!body) {
    http.end();
    snprintf(err, err_len, "NOAA: out of memory");
    return -1;
  }
  size_t len = 0, got = 0;
  const uint32_t t_dl = millis();
  const bool ok = read_body(
      http,
      [&](const uint8_t* d, int k) {
        if (len + k >= cap) return false;
        memcpy(body + len, d, k);
        len += k;
        return true;
      },
      &got, err, err_len);
  http.end();
  if (!ok) {
    free(body);
    return -1;
  }
  body[len] = 0;
  Serial.printf("[NOAA] forecast: %u bytes in %lu ms\n", (unsigned)len, (unsigned long)(millis() - t_dl));

  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, body, len, DeserializationOption::Filter(filter));
  free(body);
  if (je) {
    snprintf(err, err_len, "NOAA JSON: %s", je.c_str());
    return -1;
  }

  const char* units = doc["primaryUnits"] | "";
  const char* name = doc["primaryName"] | "";
  if (strcasecmp(units, "ft") != 0 && strcasecmp(units, "feet") != 0) {
    snprintf(err, err_len, "NOAA units '%s', not ft", units);
    return -1;
  }
  if (*name && !contains_ci(name, "stage") && !contains_ci(name, "height")) {
    snprintf(err, err_len, "NOAA series '%s', not stage", name);
    return -1;
  }

  *issued_t = 0;
  noaa_parse_time(doc["issuedTime"] | "", issued_t);

  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  if (data.isNull()) {
    snprintf(err, err_len, "NOAA: no data list");
    return -1;
  }
  int n = 0, skipped = 0;
  for (JsonObjectConst p : data) {
    int64_t t;
    JsonVariantConst v = p["primary"];
    if (!noaa_parse_time(p["validTime"] | "", &t) || v.isNull() || !v.is<float>()) { skipped++; continue; }
    float stage = v.as<float>();
    if (!isfinite(stage)) { skipped++; continue; }
    if (n >= max_pts) { skipped++; continue; }
    pts[n].t = t;
    pts[n].stage_ft = stage;
    n++;
  }
  Serial.printf("[NOAA] %s: %d points (%d skipped), issued %lld\n", gauge, n, skipped, (long long)*issued_t);
  return n;
}

namespace {
// Spots a fixed string in a byte stream, one char at a time. Fine for these
// patterns: none has a proper prefix that reappears inside it except the quote.
struct Matcher {
  const char* pat;
  int i = 0;
  bool feed(char c) {
    if (c == pat[i]) {
      if (!pat[++i]) { i = 0; return true; }
      return false;
    }
    i = c == pat[0] ? 1 : 0;
    return false;
  }
};
}  // namespace

int noaa_fetch_observed(const char* gauge, int64_t since_t, fc_point_t* pts, int max_pts, char* err,
                        size_t err_len) {
  NetworkClientSecure tls;
  HTTPClient http;
  if (!noaa_get(http, tls, gauge, "observed", err, err_len)) return -1;

  // Keys are matched with their quotes; whatever whitespace and ':' follow is skipped,
  // so pretty-printed and compact JSON both work.
  enum class St { Scan, AfterKey, Str, FindValue, Value } st = St::Scan;
  enum class Key { Units, Time, Value } key = Key::Units;
  Matcher m_units{"\"primaryUnits\""}, m_time{"\"validTime\""}, m_val{"\"primary\""};
  char tok[40], units[16] = "";
  int tl = 0, n = 0, total = 0, bad = 0;
  int64_t cur_t = 0;
  bool cur_ok = false;
  const uint32_t t0 = millis();

  auto finish_value = [&]() {
    tok[tl] = 0;
    char* end = nullptr;
    const float v = strtof(tok, &end);
    if (!cur_ok || end == tok || *end || !isfinite(v)) { bad++; return; }   // e.g. null
    total++;
    if (cur_t < since_t) return;
    if (n == max_pts) {   // keep the newest: drop the oldest
      memmove(pts, pts + 1, sizeof(fc_point_t) * (max_pts - 1));
      n--;
    }
    pts[n].t = cur_t;
    pts[n].stage_ft = v;
    n++;
  };
  auto is_space = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };

  auto feed = [&](const uint8_t* chunk, int k) {
    for (int j = 0; j < k; j++) {
      const char c = (char)chunk[j];
      switch (st) {
        case St::Scan:
          if (m_units.feed(c)) { key = Key::Units; st = St::AfterKey; }
          else if (m_time.feed(c)) { key = Key::Time; st = St::AfterKey; }
          break;
        case St::FindValue:   // after a validTime, before its primary
          if (m_val.feed(c)) { key = Key::Value; st = St::AfterKey; }
          else if (m_time.feed(c)) { bad++; key = Key::Time; st = St::AfterKey; }   // no value
          break;
        case St::AfterKey:
          if (is_space(c) || c == ':') break;
          tl = 0;
          if (key == Key::Value) {
            st = St::Value;
            tok[tl++] = c;
          } else if (c == '"') {
            st = St::Str;
          } else {
            st = St::Scan;   // not a string where one was expected
          }
          break;
        case St::Str:
          if (c != '"') {
            if (tl < (int)sizeof tok - 1) tok[tl++] = c;
            break;
          }
          tok[tl] = 0;
          if (key == Key::Units) {
            snprintf(units, sizeof units, "%s", tok);
            st = St::Scan;
          } else {
            cur_ok = noaa_parse_time(tok, &cur_t);
            st = St::FindValue;
          }
          break;
        case St::Value:
          if (c == ',' || c == '}' || c == ']' || is_space(c)) {
            finish_value();
            st = St::Scan;
          } else if (tl < (int)sizeof tok - 1) {
            tok[tl++] = c;
          }
          break;
      }
    }
    return true;
  };
  size_t got = 0;
  const bool ok = read_body(http, feed, &got, err, err_len);
  http.end();
  if (!ok) return -1;
  if (strcasecmp(units, "ft") != 0 && strcasecmp(units, "feet") != 0) {
    snprintf(err, err_len, "NOAA observed units '%s', not ft", units);
    return -1;
  }
  Serial.printf("[NOAA] %s observed: %d readings (%d skipped), kept %d, %u bytes in %lu ms\n", gauge, total, bad,
                n, (unsigned)got, (unsigned long)(millis() - t0));
  return n;
}
