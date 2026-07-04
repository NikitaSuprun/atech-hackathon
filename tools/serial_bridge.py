#!/usr/bin/env python3
"""USB bridge: relays PKT: lines between the two Pong boards' serial ports.
Usage: serial_bridge.py <controller_port> <screen_port>"""
import sys
import time

import serial

ctrl = serial.Serial(sys.argv[1], 115200, timeout=0)
scrn = serial.Serial(sys.argv[2], 115200, timeout=0)
bufs = {id(ctrl): b"", id(scrn): b""}
stats = {"c2s": 0, "s2c": 0}
last = time.time()

def pump(src, dst, key):
    data = src.read(4096)
    if not data:
        return
    bufs[id(src)] += data
    while b"\n" in bufs[id(src)]:
        line, bufs[id(src)] = bufs[id(src)].split(b"\n", 1)
        line = line.strip()
        if line.startswith(b"PKT:"):
            dst.write(line + b"\n")
            stats[key] += 1
        elif line:
            tag = "CTRL" if key == "c2s" else "SCRN"
            print(f"[{tag}] {line.decode('utf-8', 'replace')[:110]}")

print("bridge up:", sys.argv[1], "<->", sys.argv[2])
while True:
    pump(ctrl, scrn, "c2s")
    pump(scrn, ctrl, "s2c")
    if time.time() - last >= 5:
        last = time.time()
        print(f"[BRIDGE] relayed ctrl->scrn {stats['c2s']}  scrn->ctrl {stats['s2c']}")
    time.sleep(0.002)
