#include "NoaaFetch.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "noaa_roots.h"
#include "src/core/pacific_time.h"

#define NOAA_URL_FMT "https://api.water.noaa.gov/nwps/v1/gauges/%s/stageflow/forecast"

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

int noaa_fetch_forecast(const char* gauge, fc_point_t* pts, int max_pts, int64_t* issued_t,
                        char* err, size_t err_len) {
  char url[128];
  snprintf(url, sizeof url, NOAA_URL_FMT, gauge);

  NetworkClientSecure tls;
  tls.setCACert(NOAA_ROOT_CAS);
  tls.setTimeout(15);   // seconds

  HTTPClient http;
  http.useHTTP10(true);   // no chunked encoding, so the stream can go straight to the parser
  http.setTimeout(15000);
  http.setUserAgent("river-forecast-display/" "0.2");
  if (!http.begin(tls, url)) {
    snprintf(err, err_len, "NOAA: bad URL");
    return -1;
  }
  http.addHeader("Accept", "application/json");

  const int code = http.GET();
  if (code <= 0) {
    snprintf(err, err_len, "NOAA connect failed (%s)", http.errorToString(code).c_str());
    http.end();
    return -1;
  }
  if (code != 200) {
    snprintf(err, err_len, "NOAA HTTP %d", code);
    http.end();
    return -1;
  }

  JsonDocument filter;
  filter["issuedTime"] = true;
  filter["primaryName"] = true;
  filter["primaryUnits"] = true;
  filter["data"][0]["validTime"] = true;
  filter["data"][0]["primary"] = true;

  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
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
