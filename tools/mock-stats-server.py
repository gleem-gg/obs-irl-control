#!/usr/bin/env python3
# obs-irl-control
# Copyright (C) 2026 Anikeen UG (haftungsbeschränkt) & Co. KG
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version. See LICENSE for details.
"""Mock stats server for testing obs-irl-control without a real SRT relay.

Serves both formats the plugin understands from one process:

  srtrelay      GET http://127.0.0.1:18765/sockets
  Belabox Cloud GET http://127.0.0.1:18765/belabox

Control the simulated stream from the terminal it runs in (type `help`) or over HTTP:

  GET /control                 current state as JSON
  GET /control/online          publisher connected
  GET /control/offline         publisher gone (srtrelay) / connected=false (Belabox)
  GET /control/toggle          flip online <-> offline
  GET /control/rtt/<ms>        set the base RTT in milliseconds
  GET /control/flap/<seconds>  toggle online/offline automatically every N seconds (0 stops)
  GET /control/http/<code>     answer stats requests with this HTTP status (200 restores)

Plugin settings to test against this server:

  SRT Relay:     URL http://127.0.0.1:18765          publisher publish/test/
  Belabox Cloud: URL http://127.0.0.1:18765/belabox  publisher live

Only the Python standard library is used.
"""

import argparse
import json
import random
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class StreamState:
    def __init__(self, stream_id, publisher, rtt, online):
        self.lock = threading.Lock()
        self.stream_id = stream_id
        self.publisher = publisher
        self.online = online
        self.rtt = float(rtt)
        self.http_status = 200
        self.flap_interval = 0.0
        self.started = time.time()
        self.pkt_sent = 0
        self.pkt_retrans = 0
        self.last_tick = time.time()

    # --- mutation -------------------------------------------------------------

    def set_online(self, online):
        with self.lock:
            changed = self.online != online
            self.online = online
        if changed:
            log(f"stream is now {'ONLINE' if online else 'OFFLINE'}")

    def set_rtt(self, rtt):
        with self.lock:
            self.rtt = float(rtt)
        log(f"base RTT set to {rtt} ms")

    def set_http_status(self, code):
        with self.lock:
            self.http_status = int(code)
        log(f"stats endpoints now answer HTTP {code}")

    def set_flap(self, seconds):
        with self.lock:
            self.flap_interval = float(seconds)
        log(f"flapping every {seconds}s" if float(seconds) > 0 else "flapping stopped")

    # --- sampling ---------------------------------------------------------------

    def _advance_counters(self):
        now = time.time()
        elapsed = now - self.last_tick
        self.last_tick = now
        if self.online:
            self.pkt_sent += int(elapsed * 450)
            if random.random() < 0.2:
                self.pkt_retrans += random.randint(1, 3)

    def _jittered_rtt(self):
        return max(1.0, self.rtt + random.uniform(-0.05, 0.05) * self.rtt)

    def snapshot(self):
        with self.lock:
            self._advance_counters()
            return {
                "online": self.online,
                "rtt": round(self._jittered_rtt(), 2),
                "base_rtt": self.rtt,
                "http_status": self.http_status,
                "flap_interval": self.flap_interval,
                "uptime": round(time.time() - self.started, 1),
                "pkt_sent": self.pkt_sent,
                "pkt_retrans": self.pkt_retrans,
            }

    def srtrelay(self):
        s = self.snapshot()
        sockets = [
            {
                "address": "203.0.113.10:41234",
                "stream_id": "play/test/viewer",
                "stats": {"MsRTT": 12.0, "PktRecvTotal": s["pkt_sent"]},
            }
        ]
        if s["online"]:
            sockets.append(
                {
                    "address": "198.51.100.7:52000",
                    "stream_id": self.stream_id + "sender",
                    "stats": {
                        "MsTimeStamp": int(s["uptime"] * 1000),
                        "PktSentTotal": s["pkt_sent"],
                        "PktRetransTotal": s["pkt_retrans"],
                        "PktSndLossTotal": s["pkt_retrans"],
                        "MbpsSendRate": round(4.2 + random.uniform(-0.3, 0.3), 2),
                        "MbpsRecvRate": round(4.2 + random.uniform(-0.3, 0.3), 2),
                        "MsRTT": s["rtt"],
                        "MbpsBandwidth": round(38.0 + random.uniform(-5, 5), 2),
                        "PktFlightSize": random.randint(20, 60),
                        "MsRcvTsbPdDelay": 2000,
                    },
                }
            )
        return sockets

    def belabox(self):
        s = self.snapshot()
        return {
            "publishers": {
                self.publisher: {
                    "connected": s["online"],
                    "latency": 2000 if s["online"] else 0,
                    "network": 0,
                    "bitrate": random.randint(4100, 4600) if s["online"] else 0,
                    "rtt": s["rtt"] if s["online"] else 0,
                    "dropped_pkts": s["pkt_retrans"] if s["online"] else 0,
                }
            },
            "consumers": [],
        }


STATE = None
VERBOSE = False


def log(msg):
    print(f"[mock] {msg}", flush=True)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # silence per-request logging unless --verbose
        if VERBOSE:
            super().log_message(fmt, *args)

    def _send(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0].rstrip("/") or "/"
        parts = path.strip("/").split("/")

        if parts[0] == "control":
            return self._control(parts[1:])

        status = STATE.snapshot()["http_status"]
        if status != 200:
            return self._send({"error": "simulated failure"}, status)

        if path == "/sockets":
            return self._send(STATE.srtrelay())
        if path in ("/belabox", "/"):
            return self._send(STATE.belabox())
        return self._send({"error": "not found"}, 404)

    do_POST = do_GET

    def _control(self, args):
        try:
            if not args:
                pass
            elif args[0] == "online":
                STATE.set_online(True)
            elif args[0] == "offline":
                STATE.set_online(False)
            elif args[0] == "toggle":
                STATE.set_online(not STATE.snapshot()["online"])
            elif args[0] == "rtt" and len(args) > 1:
                STATE.set_rtt(float(args[1]))
            elif args[0] == "flap" and len(args) > 1:
                STATE.set_flap(float(args[1]))
            elif args[0] == "http" and len(args) > 1:
                STATE.set_http_status(int(args[1]))
            else:
                return self._send({"error": "unknown control command"}, 400)
        except ValueError:
            return self._send({"error": "invalid value"}, 400)
        return self._send(STATE.snapshot())


def flap_loop():
    while True:
        interval = STATE.snapshot()["flap_interval"]
        if interval > 0:
            time.sleep(interval)
            if STATE.snapshot()["flap_interval"] > 0:
                STATE.set_online(not STATE.snapshot()["online"])
        else:
            time.sleep(0.5)


HELP = """commands:
  online | offline | toggle   change connection state
  rtt <ms>                    set base RTT (plugin default: warn 500, max 2000)
  flap <seconds>              toggle automatically every N seconds (0 = stop)
  http <code>                 answer stats requests with this status (200 = normal)
  status                      print current state
  quit                        stop the server"""


def stdin_loop(server):
    print(HELP, flush=True)
    for line in sys.stdin:
        cmd = line.strip().split()
        if not cmd:
            continue
        try:
            if cmd[0] in ("online", "on"):
                STATE.set_online(True)
            elif cmd[0] in ("offline", "off"):
                STATE.set_online(False)
            elif cmd[0] == "toggle":
                STATE.set_online(not STATE.snapshot()["online"])
            elif cmd[0] == "rtt" and len(cmd) > 1:
                STATE.set_rtt(float(cmd[1]))
            elif cmd[0] == "flap" and len(cmd) > 1:
                STATE.set_flap(float(cmd[1]))
            elif cmd[0] == "http" and len(cmd) > 1:
                STATE.set_http_status(int(cmd[1]))
            elif cmd[0] == "status":
                log(json.dumps(STATE.snapshot()))
            elif cmd[0] in ("quit", "exit", "q"):
                server.shutdown()
                return
            else:
                print(HELP, flush=True)
        except ValueError:
            log("invalid value")
    # stdin closed (e.g. running in the background): keep serving.


def main():
    global STATE, VERBOSE
    parser = argparse.ArgumentParser(description="Mock srtrelay / Belabox Cloud stats server for obs-irl-control")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18765)
    parser.add_argument("--stream-id", default="publish/test/", help="srtrelay stream id prefix (default: publish/test/)")
    parser.add_argument("--publisher", default="live", help="Belabox Cloud publisher name (default: live)")
    parser.add_argument("--rtt", type=float, default=120, help="initial base RTT in ms (default: 120)")
    parser.add_argument("--offline", action="store_true", help="start with the stream offline")
    parser.add_argument("--verbose", action="store_true", help="log every HTTP request")
    args = parser.parse_args()

    VERBOSE = args.verbose
    STATE = StreamState(args.stream_id, args.publisher, args.rtt, not args.offline)

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    base = f"http://{args.host}:{args.port}"
    log(f"listening on {base}")
    log(f"  srtrelay:      URL {base}          publisher {args.stream_id}")
    log(f"  Belabox Cloud: URL {base}/belabox  publisher {args.publisher}")
    log(f"  control:       {base}/control/offline  {base}/control/online  {base}/control/rtt/2500")
    log(f"stream is {'ONLINE' if STATE.online else 'OFFLINE'}, base RTT {args.rtt} ms")

    threading.Thread(target=flap_loop, daemon=True).start()
    threading.Thread(target=stdin_loop, args=(server,), daemon=True).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    log("stopped")


if __name__ == "__main__":
    main()
