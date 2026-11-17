#!/usr/bin/env python3
"""Feed the NOAA VNSC1 forecast to the RP2350 display over USB serial.

The RP2350-Touch-AMOLED-1.75 has no Wi-Fi, so a PC fetches the forecast and
streams the raw points; the board itself picks the 120-hour peak and decides
FULL / PARTIAL / NONE. The board's RESULT line is checked against the Python
reference (river_forecast_t4s3/tools/check_noaa.py).

    python -m pip install pyserial
    python tools/noaa_usb_feed.py --port COM9 --once     # fetch + send once
    python tools/noaa_usb_feed.py --port COM9 --watch    # boot + 00/06/12/18 Pacific + screen taps
    python tools/noaa_usb_feed.py --port COM9 --selftest # run the board's test vectors

Works with the T4-S3 firmware too (same protocol), e.g. to inject a saved forecast:
    python tools/noaa_usb_feed.py --port COM8 --once --file saved.json
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "river_forecast_t4s3", "tools"))
from check_noaa import URL, epoch, fc_compute, pacific  # noqa: E402

SLOT_HOURS = (0, 6, 12, 18)


def load(path: str | None) -> dict:
    if path:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    req = urllib.request.Request(URL, headers={"Accept": "application/json",
                                               "User-Agent": "river-forecast-usb-feed/0.2"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


def points(doc: dict) -> tuple[list[tuple[int, float]], int]:
    units = str(doc.get("primaryUnits", "")).lower()
    if units not in ("ft", "feet"):
        raise ValueError(f"NOAA primaryUnits {units!r}, expected ft")
    pts = [(epoch(p["validTime"]), float(p["primary"]))
           for p in doc.get("data", []) if p.get("primary") is not None and p.get("validTime")]
    issued = epoch(doc["issuedTime"]) if doc.get("issuedTime") else 0
    return pts, issued


class Board:
    def __init__(self, port: str):
        try:
            import serial
        except ImportError:
            sys.exit("pyserial missing: python -m pip install pyserial")
        self.s = serial.Serial(port, 115200, timeout=0.2, write_timeout=5)
        time.sleep(0.5)
        self.s.reset_input_buffer()

    def send(self, line: str):
        self.s.write((line + "\n").encode("ascii", "replace"))

    def lines(self, seconds: float):
        end = time.time() + seconds
        while time.time() < end:
            raw = self.s.readline()
            if raw:
                yield raw.decode("ascii", "replace").strip()

    def expect(self, prefixes: tuple[str, ...], seconds: float = 5.0) -> str | None:
        for line in self.lines(seconds):
            if line.startswith(prefixes):
                return line
            if line:
                print(f"  board: {line}")
        return None


def push(board: Board, file: str | None) -> bool:
    try:
        doc = load(file)
        pts, issued = points(doc)
    except Exception as e:  # network, JSON or units problem: tell the display
        print(f"NOAA fetch failed: {e}")
        board.send(f"ERR|NOAA fetch failed: {str(e)[:40]}")
        board.expect(("OK|ERR",))
        return False

    now = int(time.time())
    board.send(f"TIME|{now}")
    if not board.expect(("OK|TIME", "NAK")):
        print("no reply to TIME (wrong port, or firmware not running?)")
        return False
    board.send(f"FC|BEGIN|VNSC1|{issued}|ft")
    if not (r := board.expect(("OK|BEGIN", "NAK"))) or r.startswith("NAK"):
        print(f"BEGIN refused: {r}")
        return False
    for t, s in pts:
        board.send(f"FC|PT|{t}|{s:.2f}")
    board.send(f"FC|END|{len(pts)}")
    got = board.expect(("RESULT", "NAK"), 10)
    print(f"sent {len(pts)} points, NOAA issued {doc.get('issuedTime')}")
    print(f"  board:     {got}")

    ref = fc_compute(pts, issued, now)
    if ref["status"] == "NONE":
        want = "RESULT|NONE"
    else:
        want = (f"RESULT|{ref['status']}|{ref['max']:.2f}|{ref['peak_t']}|{ref['covered_h']:.1f}|"
                f"{ref['flags']}|{ref['n_used']}")
        print(f"  expected:  {want}   ({ref['max']:.2f} ft, {pacific(ref['peak_t'])})")
    if not got or not got.startswith("RESULT"):
        return False
    # covered_h can differ by a second's rounding between PC and board clocks
    g, w = got.split("|"), want.split("|")
    same = g[:4] == w[:4] and g[5:] == w[5:] if len(g) == len(w) else g[:2] == w[:2]
    print("  MATCH" if same else "  MISMATCH: board and reference disagree")
    return same


def next_slot(now: float) -> float:
    t = int(now) // 60 * 60 + 60
    while True:  # minute steps are cheap and DST-proof
        local = pacific(t)                    # "Tue 11/17 06:00 AM PST"
        hh, mm = local.split()[2].split(":")
        hour = int(hh) % 12 + (12 if local.split()[3] == "PM" else 0)
        if mm == "00" and hour in SLOT_HOURS:
            return t
        t += 60


def main():
    ap = argparse.ArgumentParser(description="NOAA VNSC1 forecast -> display over USB")
    ap.add_argument("--port", required=True)
    ap.add_argument("--file", help="use a saved NOAA JSON instead of fetching")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--once", action="store_true", help="send once and exit (default)")
    mode.add_argument("--watch", action="store_true", help="keep running on the 6-hour schedule")
    mode.add_argument("--selftest", action="store_true", help="run the board's built-in tests")
    mode.add_argument("--demo", action="store_true", help="show illustrative values")
    a = ap.parse_args()

    board = Board(a.port)
    if a.selftest or a.demo:
        board.send("SELFTEST" if a.selftest else "DEMO")
        print(board.expect(("SELFTEST|PASS", "SELFTEST|FAIL|", "OK|DEMO"), 10))
        return
    ok = push(board, a.file)
    if not a.watch:
        sys.exit(0 if ok else 1)

    due = next_slot(time.time())
    print(f"next scheduled update {pacific(int(due))}")
    while True:
        for line in board.lines(1.0):
            if line == "REQ|REFRESH":
                print("screen tapped: refreshing")
                push(board, a.file)
        if time.time() >= due:
            push(board, a.file)
            due = next_slot(time.time() + 1)
            print(f"next scheduled update {pacific(int(due))}")


if __name__ == "__main__":
    main()
