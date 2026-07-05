#!/usr/bin/env python3
"""Transparent USB relay for the Atech Arcade console.

Forwards raw bytes between the brain board (which streams a COBS-framed frame
protocol out its USB-CDC serial) and the screen board (which decodes + renders
them). The console link is *binary* COBS, so unlike the old Pong ``PKT:``-line
bridge (tools/serial_bridge.py) this relays bytes verbatim in both directions.

This puts the laptop in the play loop for hardware bring-up; the wired-UART +
ESP-NOW failover link (roadmap, Stage 5b) removes it. Usage:

    uv run python tools/console_bridge.py <brain_port> <screen_port>
"""

from __future__ import annotations

import argparse
import time
from typing import Final

import serial

BAUD: Final[int] = 115200
READ_SIZE: Final[int] = 4096
POLL_SLEEP_S: Final[float] = 0.001
STATS_INTERVAL_S: Final[float] = 5.0


def main() -> None:
    ap = argparse.ArgumentParser(description="Atech Arcade console USB bridge")
    ap.add_argument("brain_port", help="serial port of the brain board")
    ap.add_argument("screen_port", help="serial port of the screen board")
    args = ap.parse_args()

    brain = serial.Serial(args.brain_port, BAUD, timeout=0)
    scrn = serial.Serial(args.screen_port, BAUD, timeout=0)
    b2s = s2b = 0
    last = time.time()
    print(
        f"console bridge up: {args.brain_port} (brain) -> {args.screen_port} (screen)"
    )

    while True:
        data = brain.read(READ_SIZE)
        if data:
            scrn.write(data)
            b2s += len(data)
        back = scrn.read(READ_SIZE)
        if back:
            brain.write(back)
            s2b += len(back)
        now = time.time()
        if now - last >= STATS_INTERVAL_S:
            last = now
            print(f"[bridge] brain->screen {b2s} B  screen->brain {s2b} B")
        time.sleep(POLL_SLEEP_S)


if __name__ == "__main__":
    main()
