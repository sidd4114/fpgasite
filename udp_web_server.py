"""
udp_server.py
Receives packets from Teensy (flags included), logs CSV, serves dashboard.

Packet format:
    F,fA1_hz,fA2_hz,fD1_hz,fD2_hz,flagA1,flagA2,flagD1,flagD2,fault|wA1|wA2|wD1|wD2

CSV columns:
    wall_time, freqA1_hz, freqA2_hz, freqD1_hz, freqD2_hz,
    rawA1, rawA2, rawD1, rawD2,
    flagA1, flagA2, flagD1, flagD2, fault
"""

import csv, json, os, pathlib, socket, threading, time
from collections import deque
from datetime import datetime
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

UDP_HOST        = "0.0.0.0"
UDP_PORT        = 5000
HTTP_HOST       = "0.0.0.0"
HTTP_PORT       = 8000
MAX_WAVE_PTS    = 600
DISCONNECT_SECS = 2.0
CHANNELS        = 4

state_lock = threading.Lock()
state = {
    "start_time":   time.time(),
    "samples":      0,
    "parse_errors": 0,
    "last_packet":  time.time(),
    "freq_hz":      [None] * CHANNELS,
    "flags":        [0]    * CHANNELS,
    "fault":        0,
    "channels":     [deque(maxlen=MAX_WAVE_PTS) for _ in range(CHANNELS)],
    "latest":       [0.0]  * CHANNELS,
}

# ── CSV session ────────────────────────────────────────────────────────────
class CSVSession:
    def __init__(self):
        ts     = datetime.now().strftime("%Y%m%d_%H%M%S")
        folder = pathlib.Path("logs") / f"session_{ts}"
        folder.mkdir(parents=True, exist_ok=True)
        path   = folder / "capture.csv"
        self._f = open(path, "w", newline="", buffering=1)
        self._w = csv.writer(self._f)
        self._w.writerow([
            "wall_time",
            "freqA1_hz", "freqA2_hz", "freqD1_hz", "freqD2_hz",
            "rawA1", "rawA2", "rawD1", "rawD2",
            "flagA1", "flagA2", "flagD1", "flagD2",
            "fault"
        ])
        print(f"[LOG]  Saving to {path}")

    def write(self, freqs, waves, flags, fault):
        self._w.writerow([
            datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3],
            round(freqs[0], 4), round(freqs[1], 4),
            round(freqs[2], 4), round(freqs[3], 4),
            int(waves[0][-1]) if waves[0] else 0,
            int(waves[1][-1]) if waves[1] else 0,
            int(waves[2][-1]) if waves[2] else 0,
            int(waves[3][-1]) if waves[3] else 0,
            flags[0], flags[1], flags[2], flags[3],
            fault
        ])

    def close(self):
        self._f.close()
        print("[LOG]  Session closed.")

# ── Packet parser ──────────────────────────────────────────────────────────
def parse_packet(payload: str):
    """
    F,fA1,fA2,fD1,fD2,flagA1,flagA2,flagD1,flagD2,fault|wA1|wA2|wD1|wD2
    Returns (freqs, waves, flags, fault) or (None,None,None,None)
    """
    try:
        if not payload.startswith("F,"):
            return None, None, None, None
        sections = payload.split("|")
        header   = sections[0].split(",")
        freqs    = [float(header[i]) for i in range(1, 5)]
        flags    = [int(float(header[i])) for i in range(5, 9)]
        fault    = int(float(header[9]))
        waves    = []
        for s in sections[1:5]:
            pts = [float(x) for x in s.split(",") if x.strip()]
            waves.append(pts)
        while len(waves) < CHANNELS:
            waves.append([])
        return freqs, waves, flags, fault
    except Exception:
        return None, None, None, None

# ── UDP listener ───────────────────────────────────────────────────────────
def udp_listener(session: CSVSession):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((UDP_HOST, UDP_PORT))
    print(f"[UDP]  Listening on {UDP_HOST}:{UDP_PORT}")

    while True:
        data, _                    = sock.recvfrom(65535)
        payload                    = data.decode(errors="ignore").strip()
        freqs, waves, flags, fault = parse_packet(payload)

        if freqs is None:
            with state_lock:
                state["parse_errors"] += 1
            continue

        session.write(freqs, waves, flags, fault)

        if fault:
            labels = ["HEALTHY", "WARNING", "FAULT"]
            names  = ["A1(pin25)", "A2(pin22)", "D1(pin24)", "D2(pin23)"]
            bad    = [f"{names[i]}={labels[min(flags[i],2)]}"
                      for i in range(4) if flags[i] > 0]
            print(f"[FAULT] {' | '.join(bad)}")

        with state_lock:
            state["samples"]    += 1
            state["last_packet"] = time.time()
            state["flags"]       = flags
            state["fault"]       = fault
            for i in range(CHANNELS):
                state["freq_hz"][i] = freqs[i] if freqs[i] > 0 else None
            for i in range(CHANNELS):
                if i < len(waves) and waves[i]:
                    state["channels"][i].extend(waves[i])
                    state["latest"][i] = waves[i][-1]

# ── Disconnect watchdog ────────────────────────────────────────────────────
def disconnect_watchdog():
    while True:
        time.sleep(0.5)
        with state_lock:
            if time.time() - state["last_packet"] > DISCONNECT_SECS:
                for i in range(CHANNELS):
                    state["channels"][i].extend([0.0] * 10)
                    state["freq_hz"][i] = None
                state["latest"] = [0.0] * CHANNELS
                state["flags"]  = [0]   * CHANNELS
                state["fault"]  = 0

# ── HTTP handler ───────────────────────────────────────────────────────────
def build_payload():
    with state_lock:
        connected = (time.time() - state["last_packet"]) < DISCONNECT_SECS
        return {
            "channels":     [list(ch) for ch in state["channels"]],
            "latest":       list(state["latest"]),
            "freq_hz":      list(state["freq_hz"]),
            "flags":        list(state["flags"]),
            "fault":        state["fault"],
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
    session  = CSVSession()
    web_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")
    os.makedirs(web_root, exist_ok=True)
    os.chdir(web_root)
    threading.Thread(target=udp_listener, args=(session,), daemon=True).start()
    threading.Thread(target=disconnect_watchdog, daemon=True).start()
    print(f"[HTTP] Dashboard → http://localhost:{HTTP_PORT}")
    print("       Ctrl+C to stop.\n")
    server = ThreadingHTTPServer((HTTP_HOST, HTTP_PORT), DashboardHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        session.close()
        print("\nStopped.")