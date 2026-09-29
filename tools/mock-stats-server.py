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
  Gleem IRL     GET http://127.0.0.1:18765/api/v1/irl/devices[/<uuid>]
                (needs "Authorization: Bearer mock-token", like the gleem.gg Developer API)

Control the simulated stream from the terminal it runs in (type `help`) or over HTTP:

  GET /control                 current state as JSON
  GET /control/online          publisher connected
  GET /control/offline         publisher gone (srtrelay) / connected=false (Belabox)
  GET /control/toggle          flip online <-> offline
  GET /control/rtt/<ms>        set the base RTT in milliseconds
  GET /control/flap/<seconds>  toggle online/offline automatically every N seconds (0 stops)
  GET /control/http/<code>     answer stats requests with this HTTP status (200 restores)
  GET /control/publishing/on   Gleem: ingest passes the stream on to viewers (default)
  GET /control/publishing/off  Gleem: the box streams, but ingest does not publish it

Plugin settings to test against this server:

  SRT Relay:     URL http://127.0.0.1:18765          publisher publish/test/
  Belabox Cloud: URL http://127.0.0.1:18765/belabox  publisher live
  Gleem IRL:     API URL http://127.0.0.1:18765  token mock-token  box empty (or the mock uuid)

Only the Python standard library is used.
"""

import argparse
import datetime
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
        self.publishing = True
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

    def set_publishing(self, publishing):
        with self.lock:
            self.publishing = publishing
        log(f"ingest publishing {'ON' if publishing else 'OFF'}")

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
                "publishing": self.publishing,
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


    def gleem_device(self):
        """One device in the shape of gleem.gg's GET /api/v1/irl/devices/{uuid}."""
        s = self.snapshot()
        now = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
        iso = lambda t: t.isoformat()
        stream = None
        if s["online"]:
            wwan0_bps = random.randint(2800000, 3200000)
            wwan1_bps = random.randint(900000, 1200000)
            wwan0_rtt = round(max(1.0, s["rtt"] - 10 + random.uniform(-3, 3)), 1)
            wwan1_rtt = round(s["rtt"] + 40 + random.uniform(-5, 5), 1)
            links = [
                {"link_id": 0, "iface": "wwan0", "state": "live", "rtt_ms": wwan0_rtt,
                 "loss_pct": round(random.uniform(0, 0.8), 2), "tx_bps": wwan0_bps},
                {"link_id": 1, "iface": "wwan1", "state": "live", "rtt_ms": wwan1_rtt,
                 "loss_pct": round(random.uniform(0, 2), 2), "tx_bps": wwan1_bps},
            ]
            rtt = round((wwan0_rtt * wwan0_bps + wwan1_rtt * wwan1_bps) / (wwan0_bps + wwan1_bps), 1)
            stream = {
                "state": "live",
                "sid": "0f8e7d6c-5b4a-4938-8271-605f4e3d2c1b",
                "started_at": iso(now - datetime.timedelta(seconds=s["uptime"])),
                "error": None,
                "encoder_connected": True,
                "bitrate_bps": wwan0_bps + wwan1_bps,
                "rtt_ms": rtt,
                "links": links,
                "healthy": s["publishing"],
            }
        ingest = None
        if s["online"]:
            ingest = {
                "region": "mock",
                "publishing": s["publishing"],
                "last_reported_at": iso(now),
                "average_bps": 4100000,
                "peak_bps": 4600000,
            }
        return {
            "uuid": GLEEM_UUID,
            "name": "Mock Backpack",
            # The box keeps heartbeating while idle: "offline" here means the stream stopped.
            "online": True,
            "last_seen_at": iso(now),
            "stream": stream,
            "ingest": ingest,
            "poll_interval_seconds": 2,
        }


GLEEM_UUID = "9d3c2f4e-6b1a-4c8e-9f2d-1a2b3c4d5e6f"
GLEEM_TOKEN = "mock-token"
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

        if path.startswith("/api/v1/irl/devices"):
            return self._gleem(path)
        if path == "/sockets":
            return self._send(STATE.srtrelay())
        if path in ("/belabox", "/"):
            return self._send(STATE.belabox())
        return self._send({"error": "not found"}, 404)

    do_POST = do_GET

    def _gleem(self, path):
        if self.headers.get("Authorization", "") != f"Bearer {GLEEM_TOKEN}":
            return self._send({"error": {"code": "unauthenticated", "message": "Missing or invalid API token."}}, 401)
        if path == "/api/v1/irl/devices":
            return self._send({"data": [STATE.gleem_device()]})
        if path == f"/api/v1/irl/devices/{GLEEM_UUID}":
            return self._send(STATE.gleem_device())
        return self._send({"error": {"code": "not_found", "message": "Not found."}}, 404)

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
            elif args[0] == "publishing" and len(args) > 1:
                STATE.set_publishing(args[1] in ("on", "1", "true"))
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
  publishing on|off           Gleem: whether ingest passes the stream on
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
            elif cmd[0] == "publishing" and len(cmd) > 1:
                STATE.set_publishing(cmd[1] in ("on", "1", "true"))
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
    log(f"  Gleem IRL:     API URL {base}  token {GLEEM_TOKEN}  box empty or {GLEEM_UUID}")
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
