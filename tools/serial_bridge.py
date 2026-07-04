#!/usr/bin/env python3
"""USB bridge: relays PKT: lines between the two Pong boards' serial ports."""

from __future__ import annotations

import argparse
import time
from typing import Final

import serial

BAUD: Final[int] = 115200
READ_SIZE: Final[int] = 4096
PKT_PREFIX: Final[bytes] = b"PKT:"
LOG_TRUNC: Final[int] = 110
STATS_INTERVAL_S: Final[float] = 5.0
POLL_SLEEP_S: Final[float] = 0.002


def pump(
    src: serial.Serial,
    dst: serial.Serial,
    bufs: dict[int, bytes],
    stats: dict[str, int],
    key: str,
) -> None:
    data = src.read(READ_SIZE)
    if not data:
        return
    bufs[id(src)] += data
    while b"\n" in bufs[id(src)]:
        line, bufs[id(src)] = bufs[id(src)].split(b"\n", 1)
        line = line.strip()
        if line.startswith(PKT_PREFIX):
            dst.write(line + b"\n")
            stats[key] += 1
        elif line:
            tag = "CTRL" if key == "c2s" else "SCRN"
            print(f"[{tag}] {line.decode('utf-8', 'replace')[:LOG_TRUNC]}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("controller_port", help="serial port of the controller board")
    ap.add_argument("screen_port", help="serial port of the screen board")
    args = ap.parse_args()

    ctrl = serial.Serial(args.controller_port, BAUD, timeout=0)
    scrn = serial.Serial(args.screen_port, BAUD, timeout=0)
    bufs: dict[int, bytes] = {id(ctrl): b"", id(scrn): b""}
    stats: dict[str, int] = {"c2s": 0, "s2c": 0}
    last = time.time()

    print("bridge up:", args.controller_port, "<->", args.screen_port)
    while True:
        pump(ctrl, scrn, bufs, stats, "c2s")
        pump(scrn, ctrl, bufs, stats, "s2c")
        if time.time() - last >= STATS_INTERVAL_S:
            last = time.time()
            print(
                f"[BRIDGE] relayed ctrl->scrn {stats['c2s']}  scrn->ctrl {stats['s2c']}"
            )
        time.sleep(POLL_SLEEP_S)


if __name__ == "__main__":
    main()
