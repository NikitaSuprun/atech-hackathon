#!/usr/bin/env python3
"""Impersonates the controller board so the screen server (state machine,
protocol, rendering) is testable with zero controller hardware.

Join the screen board's SoftAP (SSID atech-pong) first, then run:
    python3 tools/fake_controller.py

Keys: a/z = knob0 -/+2 detents, k/m = knob1 -/+2,
      s = toggle P1 hold, l = toggle P2 hold, q = quit
"""

import os
import select
import socket
import struct
import sys
import termios
import time
import tty

SERVER = ("192.168.4.1", 47420)
LOCAL_PORT = 47421

MAGIC = 0x474E4F50
VERSION = 2
PKT_INPUT = 0x01
PKT_FEEDBACK = 0x02

# Mirrors pong_proto.h: PongInputPacket (24 B), PongFeedbackPacket (26 B).
INPUT_FMT = "<IBBBBIIii"
FEEDBACK_FMT = "<IBBBBI" + "B" * 14

INPUT_PERIOD = 0.020
LINK_TIMEOUT = 2.5

STATE_NAMES = ("GS_LINK_WAIT", "GS_READY_CHECK", "GS_COUNTDOWN",
               "GS_PLAYING", "GS_POINT_FLASH", "GS_GAME_OVER")

assert struct.calcsize(INPUT_FMT) == 24
assert struct.calcsize(FEEDBACK_FMT) == 26
assert len(struct.pack(INPUT_FMT, MAGIC, VERSION, PKT_INPUT, 0, 0, 0, 0, 0, 0)) == 24


def u8_adv(new, old):
    return (new - old) & 0xFF


def u32_ahead(new, old):
    return 0 < ((new - old) & 0xFFFFFFFF) < 0x80000000


def out(s):
    sys.stdout.write(s)
    sys.stdout.flush()


class FakeController:
    def __init__(self, sock):
        self.sock = sock
        self.t0 = time.monotonic()
        self.knob = [0, 0]
        self.held = 0
        self.tx_seq = 0
        self.running = True
        self.last_rx = self.t0
        self.link_warned = False
        self.rx_seq = None
        self.counters = None
        self.fb = None
        self.last_status = None

    def handle_keys(self):
        for ch in os.read(sys.stdin.fileno(), 64).decode("ascii", "ignore"):
            if ch == "a":
                self.knob[0] -= 2
            elif ch == "z":
                self.knob[0] += 2
            elif ch == "k":
                self.knob[1] -= 2
            elif ch == "m":
                self.knob[1] += 2
            elif ch == "s":
                self.held ^= 0x01
            elif ch == "l":
                self.held ^= 0x02
            elif ch in ("q", "\x03"):
                self.running = False

    def send_input(self, now):
        uptime_ms = int((now - self.t0) * 1000) & 0xFFFFFFFF
        pkt = struct.pack(INPUT_FMT, MAGIC, VERSION, PKT_INPUT, 0, self.held,
                          self.tx_seq, uptime_ms, self.knob[0], self.knob[1])
        try:
            self.sock.sendto(pkt, SERVER)
        except OSError:
            pass
        self.tx_seq = (self.tx_seq + 1) & 0xFFFFFFFF

    def pump_rx(self, now):
        while True:
            try:
                data, _ = self.sock.recvfrom(2048)
            except OSError:
                return
            if len(data) != 26:
                continue
            f = struct.unpack(FEEDBACK_FMT, data)
            if f[0] != MAGIC or f[1] != VERSION or f[2] != PKT_FEEDBACK:
                continue
            self.take_feedback(f, now)

    def take_feedback(self, f, now):
        (_, _, _, _target, state, seq, s0, s1, held_echo, r0, r1,
         hit, wall, goal, goal_by, win, winner, serve, serving, _pad) = f
        if self.rx_seq is not None and not u32_ahead(seq, self.rx_seq):
            return
        self.rx_seq = seq
        self.last_rx = now
        cues = {"hit": hit, "wall": wall, "goal": goal, "win": win, "serve": serve}
        # Cold start / post-gap: adopt counters without firing (no replayed cues).
        if self.counters is None or self.link_warned:
            self.link_warned = False
            self.cue_line("LINK UP — feedback flowing")
        else:
            self.fire_cues(cues, goal_by, winner)
        self.counters = cues
        self.fb = (state, s0, s1, held_echo, r0, r1, serving)

    def fire_cues(self, cues, goal_by, winner):
        old = self.counters
        if u8_adv(cues["serve"], old["serve"]):
            self.cue_line("CUE serve")
        if u8_adv(cues["hit"], old["hit"]):
            self.cue_line("CUE hit")
        if u8_adv(cues["wall"], old["wall"]):
            self.cue_line("CUE wall")
        if u8_adv(cues["goal"], old["goal"]):
            self.cue_line("CUE goal by=%d" % goal_by)
        if u8_adv(cues["win"], old["win"]):
            self.cue_line("CUE win winner=%d" % winner)

    def cue_line(self, msg):
        out("\r\x1b[K%s\r\n" % msg)
        self.last_status = None

    def tick_link(self, now):
        if not self.link_warned and now - self.last_rx > LINK_TIMEOUT:
            self.link_warned = True
            self.cue_line("LINK LOST — is this laptop on the atech-pong WiFi?")

    def draw_status(self):
        if self.fb is None:
            base = "waiting for feedback"
        else:
            state, s0, s1, echo, r0, r1, _serving = self.fb
            name = STATE_NAMES[state] if state < len(STATE_NAMES) else "GS_%d" % state
            base = "%-14s score %d-%d  rdy %3d/%3d  echo %d%d" % (
                name, s0, s1, r0, r1, echo & 1, (echo >> 1) & 1)
        line = "%s | hold s:%s l:%s | knob %+d/%+d" % (
            base,
            "ON " if self.held & 1 else "off",
            "ON " if self.held & 2 else "off",
            self.knob[0], self.knob[1])
        if self.link_warned:
            line += " | NO LINK"
        if line != self.last_status:
            out("\r\x1b[K" + line)
            self.last_status = line

    def run(self):
        stdin_fd = sys.stdin.fileno()
        next_tx = time.monotonic()
        while self.running:
            now = time.monotonic()
            if now >= next_tx:
                self.send_input(now)
                next_tx += INPUT_PERIOD
                if next_tx < now:
                    next_tx = now + INPUT_PERIOD
            self.tick_link(now)
            self.draw_status()
            timeout = max(0.0, next_tx - time.monotonic())
            try:
                readable, _, _ = select.select([self.sock, stdin_fd], [], [], timeout)
            except InterruptedError:
                continue
            now = time.monotonic()
            if self.sock in readable:
                self.pump_rx(now)
            if stdin_fd in readable:
                self.handle_keys()


def main():
    if not sys.stdin.isatty():
        sys.exit("fake_controller needs an interactive terminal")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("", LOCAL_PORT))
    except OSError as e:
        sys.exit("cannot bind UDP port %d: %s" % (LOCAL_PORT, e))
    sock.setblocking(False)
    print("fake_controller: :%d -> %s:%d @ 50 Hz" % (LOCAL_PORT, SERVER[0], SERVER[1]))
    print("keys: a/z knob0  k/m knob1  s/l hold P1/P2  q quit")
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd)
    tty.setraw(fd)
    try:
        FakeController(sock).run()
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)
        sock.close()
        print()


if __name__ == "__main__":
    main()
