import json
import os
import re
import socket
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

UDP_HOST = "0.0.0.0"
UDP_PORT = 5000
HTTP_HOST = "0.0.0.0"
HTTP_PORT = 8000
MAX_POINTS = 240
CHANNELS = 4

number_re = re.compile(r"-?\d+(?:\.\d+)?")

state_lock = threading.Lock()
state = {
    "start_time": time.time(),
    "samples": 0,
    "parse_errors": 0,
    "timestamps": [],
    "channels": [[] for _ in range(CHANNELS)],
    "latest": [0.0] * CHANNELS,
}


def parse_packet(payload: str):
    values = [float(x) for x in number_re.findall(payload)]
    if not values:
        return None
    if len(values) < CHANNELS:
        last_value = values[-1]
        values = values + [last_value] * (CHANNELS - len(values))
    return values[:CHANNELS]


def udp_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_HOST, UDP_PORT))
    print(f"Listening for UDP on {UDP_HOST}:{UDP_PORT}")

    while True:
        data, addr = sock.recvfrom(2048)
        payload = data.decode(errors="ignore")
        values = parse_packet(payload)
        with state_lock:
            if values is None:
                state["parse_errors"] += 1
                continue

            state["samples"] += 1
            state["latest"] = values
            state["timestamps"].append(time.time())
            for idx in range(CHANNELS):
                state["channels"][idx].append(values[idx])

            if len(state["channels"][0]) > MAX_POINTS:
                state["timestamps"].pop(0)
                for idx in range(CHANNELS):
                    state["channels"][idx].pop(0)


def build_payload():
    with state_lock:
        uptime = int(time.time() - state["start_time"])
        payload = {
            "values": state["latest"],
            "samples": state["samples"],
            "uptime": uptime,
            "parse_errors": state["parse_errors"],
        }
    return payload


class DashboardHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/data":
            payload = build_payload()
            body = json.dumps(payload).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        super().do_GET()


if __name__ == "__main__":
    web_root = os.path.join(os.path.dirname(__file__), "web")
    os.chdir(web_root)

    thread = threading.Thread(target=udp_listener, daemon=True)
    thread.start()

    print(f"Serving dashboard from {web_root}")
    print(f"Open http://localhost:{HTTP_PORT} in your browser")

    server = ThreadingHTTPServer((HTTP_HOST, HTTP_PORT), DashboardHandler)
    server.serve_forever()
