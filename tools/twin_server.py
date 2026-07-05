#!/usr/bin/env python3
"""One-command deploy for the Atech Arcade console.

Runs the brain<->screen bridge (so the PHYSICAL wall lights) AND serves the
browser dashboard + a live brain stream over Server-Sent Events, all bound to
0.0.0.0. Because the dashboard gets its data over HTTP (not Web Serial), the
physical wall and any number of network dashboards run live at the same time --
no more one-USB-port either/or.

    .venv/bin/python tools/twin_server.py [port]

Then open  http://<this-machine-ip>:<port>/  on any device on the LAN.
The dashboard auto-connects to /stream and mirrors the console (matrix + the
real TFT via WASM + live knobs). Ctrl-C to stop.
"""
import base64
import glob
import http.server
import os
import queue
import socket
import socketserver
import sys
import threading
import time

import serial  # pyserial (in .venv)

DASH_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "host", "dashboard"))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080

# --- SSE client registry: each connected dashboard gets a byte queue ---
_clients = set()
_lock = threading.Lock()


def broadcast(data: bytes):
    with _lock:
        dead = []
        for q in _clients:
            try:
                q.put_nowait(data)
            except queue.Full:
                dead.append(q)  # a stalled client: drop it rather than block the wall
        for q in dead:
            _clients.discard(q)


def id_ports():
    """Return (brain, screen): the brain continuously streams CFRM frames; the
    screen is silent. Sniff each port's byte rate to tell them apart."""
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) < 2:
        return None, None

    def rate(p):
        try:
            s = serial.Serial(p, 115200, timeout=0.2)
        except Exception:
            return -1
        time.sleep(0.1)
        s.reset_input_buffer()
        d = bytearray()
        t = time.time()
        while time.time() - t < 1.0:
            d += s.read(8192)
        s.close()
        return len(d)

    r = {p: rate(p) for p in ports}
    brain = max(r, key=lambda p: r[p])
    screen = next(p for p in ports if p != brain)
    return brain, screen


def bridge_loop():
    """Relay brain<->screen forever (auto-detect + auto-reconnect), and fan the
    brain->host bytes out to the SSE clients."""
    while True:
        brain_p, screen_p = id_ports()
        if not brain_p or not screen_p:
            print("[twin] waiting for both boards to appear...")
            time.sleep(1.5)
            continue
        try:
            # brain: block briefly for the first byte then take everything buffered,
            # so the thread sleeps when idle but drains continuously once frames flow
            # (no busy-poll). screen: non-blocking -- it is silent by design, so a
            # blocking read there would stall the whole relay.
            brain = serial.Serial(brain_p, 115200, timeout=0.02)
            screen = serial.Serial(screen_p, 115200, timeout=0)
        except Exception as e:
            print("[twin] open failed:", e)
            time.sleep(1.5)
            continue
        print(f"[twin] bridge up: {brain_p} (brain) -> {screen_p} (screen)  +  SSE fan-out")
        try:
            while True:
                d = brain.read(1)
                if d:
                    n = brain.in_waiting
                    if n:
                        d += brain.read(n)
                    screen.write(d)   # feed the physical wall
                    broadcast(d)      # feed every network dashboard
                back = screen.read(256)  # non-blocking: returns at once
                if back:
                    brain.write(back)
        except Exception as e:
            print("[twin] bridge dropped, reconnecting:", e)
            for s in (brain, screen):
                try:
                    s.close()
                except Exception:
                    pass
            time.sleep(1.5)


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=DASH_DIR, **k)

    def log_message(self, format, *args):
        pass  # quiet

    def do_GET(self):
        if self.path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            q = queue.Queue(maxsize=512)
            with _lock:
                _clients.add(q)
            try:
                while True:
                    data = q.get()
                    self.wfile.write(b"data:" + base64.b64encode(data) + b"\n\n")
                    self.wfile.flush()
            except Exception:
                pass
            finally:
                with _lock:
                    _clients.discard(q)
        else:
            super().do_GET()


class ThreadingHTTP(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        pass  # SSE clients disconnect routinely — don't spew tracebacks


def lan_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


if __name__ == "__main__":
    threading.Thread(target=bridge_loop, daemon=True).start()
    srv = ThreadingHTTP(("0.0.0.0", PORT), Handler)
    print(f"[twin] dashboard live at  http://{lan_ip()}:{PORT}/   (also http://localhost:{PORT}/)")
    print("[twin] physical wall + network dashboards run together. Ctrl-C to stop.")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n[twin] bye")
