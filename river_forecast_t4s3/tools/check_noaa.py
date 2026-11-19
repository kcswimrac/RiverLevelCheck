#!/usr/bin/env python3
"""Fetch NOAA VNSC1 (or read a saved JSON) and print what the display should show.

Python mirror of src/core/forecast.c, for checking the board's RESULT line:
    python -I tools/check_noaa.py              # live NOAA
    python -I tools/check_noaa.py saved.json   # fixture
No third-party packages needed.
"""
import json
import math
import sys
import time
import urllib.request
from datetime import datetime, timedelta, timezone

URL = "https://api.water.noaa.gov/nwps/v1/gauges/VNSC1/stageflow/forecast"
H = 3600
HORIZON, LOOKBACK, END_TOL, MAX_GAP, MAX_AGE = 120 * H, H, H, 6 * H, 36 * H
SHORT, GAP, OLD, PEAK_AT_END = 1, 2, 4, 8


def epoch(s):
    return int(datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp())


def fc_compute(points, issued, now):
    pts = sorted(points)
    lo, hi = now - LOOKBACK, now + HORIZON
    used = [(t, s) for t, s in pts if lo <= t <= hi and math.isfinite(s) and -50 <= s <= 200]
    if not used:
        return dict(status="NONE")
    flags = 0
    mx, peak_t, peak_prev = used[0][1], used[0][0], None
    for i in range(1, len(used)):
        t, s = used[i]
        if t - used[i - 1][0] > MAX_GAP:
            flags |= GAP
        if s > mx:
            mx, peak_t, peak_prev = s, t, used[i - 1][1]
    last_t = used[-1][0]
    if used[0][0] - now > MAX_GAP:
        flags |= GAP
    if last_t < hi - END_TOL:
        flags |= SHORT
        if peak_t == last_t and peak_prev is not None and mx > peak_prev:
            flags |= PEAK_AT_END
    if issued > 0 and now - issued > MAX_AGE:
        flags |= OLD
    return dict(status="PARTIAL" if flags else "FULL", max=mx, peak_t=peak_t,
                covered_h=max(0, last_t - now) / H, flags=flags, n_used=len(used))


def pacific(t):
    """US rule, mirrors pacific_time.c."""
    u = datetime.fromtimestamp(t, timezone.utc)
    def nth_sunday(y, m, n):
        d = datetime(y, m, 1, tzinfo=timezone.utc)
        return d + timedelta(days=(6 - d.weekday()) % 7 + 7 * (n - 1))
    start = nth_sunday(u.year, 3, 2) + timedelta(hours=10)
    end = nth_sunday(u.year, 11, 1) + timedelta(hours=9)
    dst = start <= u < end
    return (u + timedelta(hours=-7 if dst else -8)).strftime("%a %m/%d %I:%M %p ") + ("PDT" if dst else "PST")


def main():
    if len(sys.argv) > 1:
        doc = json.load(open(sys.argv[1]))
    else:
        req = urllib.request.Request(URL, headers={"Accept": "application/json",
                                                   "User-Agent": "river-forecast-check/0.2"})
        doc = json.load(urllib.request.urlopen(req, timeout=20))
    assert doc.get("primaryUnits", "").lower() in ("ft", "feet"), doc.get("primaryUnits")
    pts = [(epoch(p["validTime"]), float(p["primary"])) for p in doc["data"] if p.get("primary") is not None]
    issued = epoch(doc["issuedTime"]) if doc.get("issuedTime") else 0
    now = int(time.time())
    r = fc_compute(pts, issued, now)
    print(f"issued {doc.get('issuedTime')}, {len(pts)} points, now {pacific(now)}")
    if r["status"] == "NONE":
        print("RESULT|NONE")
        return
    print(f"RESULT|{r['status']}|{r['max']:.2f}|{r['peak_t']}|{r['covered_h']:.1f}|{r['flags']}|{r['n_used']}")
    print(f"max {r['max']:.2f} ft at {pacific(r['peak_t'])}; covers {r['covered_h']:.1f} of 120 h")
    if len(sys.argv) == 1:
        print_observed(now)


def print_observed(now):
    """Latest gauge reading, as the board's OBS| line reports it."""
    req = urllib.request.Request(URL.replace("/forecast", "/observed"),
                                 headers={"Accept": "application/json", "User-Agent": "river-forecast-check/0.4"})
    doc = json.load(urllib.request.urlopen(req, timeout=30))
    obs = [(epoch(p["validTime"]), float(p["primary"])) for p in doc["data"]
           if p.get("primary") is not None and epoch(p["validTime"]) <= now + 600]
    if not obs:
        print("OBS|NONE")
        return
    t, ft = max(obs)
    print(f"OBS|OK|{ft:.2f}|{t}  (reading {pacific(t)})")


if __name__ == "__main__":
    main()
