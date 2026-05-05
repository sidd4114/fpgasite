"""
udp_server.py
─────────────────────────────────────────────────────────────────────────────
Receives packets from Teensy, serves freq + waveform to dashboard via HTTP.

Packet format (every 100ms from Teensy):
    F,<freqA1_hz>,<freqA2_hz>,<freqD1_hz>,<freqD2_hz>|<wA1>|<wA2>|<wD1>|<wD2>\n

Run:  python udp_server.py
Open: http://localhost:8000
"""

import json, os, socket, threading, time
from collections import deque
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

UDP_HOST         = "0.0.0.0"
UDP_PORT         = 5000
HTTP_HOST        = "0.0.0.0"
HTTP_PORT        = 8000
MAX_WAVE_PTS     = 600        # ring buffer depth per channel (waveform display)
DISCONNECT_SECS  = 2.0
CHANNELS         = 4

state_lock = threading.Lock()
state = {
    "start_time":  time.time(),
    "samples":     0,
    "parse_errors": 0,
    "last_packet": time.time(),
    "freq_hz":     [None] * CHANNELS,   # hardware-measured, sent from Teensy
    "channels":    [deque(maxlen=MAX_WAVE_PTS) for _ in range(CHANNELS)],
    "latest":      [0.0] * CHANNELS,
}


def parse_packet(payload: str):
    """
    Parse:  F,fA1,fA2,fD1,fD2|wA1_0,wA1_1,...|wA2_0,...|wD1_0,...|wD2_0,...
    Returns (freqs: list[float], waves: list[list[float]]) or (None, None).
    """
    try:
        if not payload.startswith("F,"):
            return None, None
        sections = payload.split("|")
        # Header: F, fA1, fA2, fD1, fD2
        header = sections[0].split(",")
        freqs  = [float(header[i]) for i in range(1, 5)]
        # Waveform sections
        waves = []
        for s in sections[1:5]:
            pts = [float(x) for x in s.split(",") if x.strip()]
            waves.append(pts)
        # Pad missing channels
        while len(waves) < CHANNELS:
            waves.append([])
        return freqs, waves
    except Exception:
        return None, None


def udp_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((UDP_HOST, UDP_PORT))
    print(f"[UDP]  Listening on {UDP_HOST}:{UDP_PORT}")
    while True:
        data, _ = sock.recvfrom(65535)
        payload  = data.decode(errors="ignore").strip()
        freqs, waves = parse_packet(payload)
        with state_lock:
            if freqs is None:
                state["parse_errors"] += 1
                continue
            state["samples"]    += 1
            state["last_packet"] = time.time()
            # Store hardware-measured frequencies directly — no estimation needed
            for i in range(CHANNELS):
                state["freq_hz"][i] = freqs[i] if freqs[i] > 0 else None
            # Store waveform data
            for i in range(CHANNELS):
                if i < len(waves) and waves[i]:
                    state["channels"][i].extend(waves[i])
                    state["latest"][i] = waves[i][-1]


def disconnect_watchdog():
    """Zero-fill waveforms and clear freqs if Teensy goes silent."""
    while True:
        time.sleep(0.5)
        with state_lock:
            if time.time() - state["last_packet"] > DISCONNECT_SECS:
                for i in range(CHANNELS):
                    state["channels"][i].extend([0.0] * 10)
                    state["freq_hz"][i] = None
                state["latest"] = [0.0] * CHANNELS


def build_payload():
    with state_lock:
        connected = (time.time() - state["last_packet"]) < DISCONNECT_SECS
        return {
            "channels":     [list(ch) for ch in state["channels"]],
            "latest":       list(state["latest"]),
            "freq_hz":      list(state["freq_hz"]),
            "samples":      state["samples"],
            "uptime":       int(time.time() - state["start_time"]),
            "parse_errors": state["parse_errors"],
            "connected":    connected,
        }


class DashboardHandler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args): pass
    def do_GET(self):
        if self.path == "/data":
            body = json.dumps(build_payload()).encode()
            self.send_response(200)
            self.send_header("Content-Type",   "application/json")
            self.send_header("Cache-Control",  "no-store")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()


if __name__ == "__main__":
    web_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
    os.makedirs(web_root, exist_ok=True)
    os.chdir(web_root)
    threading.Thread(target=udp_listener,        daemon=True).start()
    threading.Thread(target=disconnect_watchdog,  daemon=True).start()
    print(f"[HTTP] Dashboard → http://localhost:{HTTP_PORT}")
    print(f"       Web root : {web_root}")
    print("       Ctrl+C to stop.\n")
    server = ThreadingHTTPServer((HTTP_HOST, HTTP_PORT), DashboardHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")