#pragma once
#include <stddef.h>
#include <stdint.h>

#include "src/core/forecast.h"

// GET https://api.water.noaa.gov/nwps/v1/gauges/<gauge>/stageflow/forecast over
// verified TLS, keep only issuedTime / primaryName / primaryUnits / data[].validTime /
// data[].primary, and refuse anything that is not stage in feet.
// Returns the number of points (>= 0) or -1 with a short display-ready message in err.
int noaa_fetch_forecast(const char* gauge, fc_point_t* pts, int max_pts, int64_t* issued_t,
                        char* err, size_t err_len);

// "2026-11-17T16:00:00Z" or "...+00:00" -> Unix seconds; false if malformed.
bool noaa_parse_time(const char* s, int64_t* out);
