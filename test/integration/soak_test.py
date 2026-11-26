"""Long-duration soak (Phase 12 gate 4): keeps the controller busy and logs
its health once a minute.

    python3 soak_test.py http://lumen-0798.local '<admin password>' --hours 24 --out test/soak/results

Load: light commands every ~2 s (on/off, brightness, fades, effects), a scene
every 30 s, an automation that echoes one light onto another, a WebSocket
client kept open, MQTT left connected. Every 60 s: heap free / min / largest
block, uptime (a drop = reboot), lighting overruns, queue drops, hardware
errors, command latency, WS drops, Wi-Fi reconnects. Network outages (the
PC changing networks) are logged and the run continues; the controller is
found again through mDNS. Writes <out>/soak-<start>.csv and a summary.
Uses PCA9685 channels 8–9 (lights soak_a / soak_b, removed at the end).
"""
import argparse
import asyncio
import csv
import json
import os
import random
import socket
import sys
import threading
import time

import requests

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "device"))
from lumen_client import Lumen  # noqa: E402

FIELDS = ["time", "elapsed_h", "reachable", "uptime_s", "heap_free", "heap_min", "heap_largest", "ticks", "tick_overruns",
          "max_tick_us", "queue_dropped", "hw_errors", "cmd_lat_avg_us", "cmd_lat_max_us", "ws_clients", "ws_dropped",
          "wifi_reconnects", "rssi", "mqtt_connected", "boot_count", "reset_reason", "commands_sent", "command_errors", "reboots"]


class Soak:
    def __init__(self, url, password, out):
        self.url, self.password = url.rstrip("/"), password
        self.L = None
        self.stop = threading.Event()
        self.sent = 0
        self.errors = 0
        self.lock = threading.Lock()
        self.out = out

    def session(self):
        host = self.url.split("//", 1)[1].split("/")[0]
        try:
            socket.getaddrinfo(host, 80)  # re-resolve mDNS after network changes
        except OSError:
            pass
        L = Lumen(self.url)
        L.login("admin", self.password)
        self.L = L
        return L

    def call(self, fn):
        for attempt in range(2):
            try:
                if self.L is None:
                    self.session()
                return fn(self.L)
            except Exception:  # noqa: BLE001 — log and retry once with a new session
                self.L = None
                if attempt:
                    raise
                time.sleep(2)

    def setup(self):
        L = self.session()
        for lid, ch in (("soak_a", 8), ("soak_b", 9)):
            try:
                L.delete(f"/lights/{lid}")
            except Exception:  # noqa: BLE001
                pass
            L.post("/lights", {"id": lid, "name": lid, "type": "dimmer", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": ch}, "transitionMs": 0})
        for path in ("/automations/soak_echo", "/scenes/soak_scene"):
            try:
                L.delete(path)
            except Exception:  # noqa: BLE001
                pass
        L.post("/automations", {"id": "soak_echo", "name": "Soak echo", "enabled": True,
                                "trigger": {"type": "light_state", "light_id": "soak_a", "state": True},
                                "actions": [{"type": "light_brightness", "light_id": "soak_b", "brightness": 60, "transition_ms": 300}],
                                "execution": {"cooldown_ms": 0, "max_executions_per_hour": 3600}})
        L.post("/lights/soak_a/command", {"power": True, "brightness": 50, "transitionMs": 0})
        time.sleep(0.3)
        L.post("/scenes/capture", {"id": "soak_scene", "name": "Soak scene", "lightIds": ["soak_a", "soak_b"], "transitionMs": 500})

    def load(self):
        effects = ["breathing", "pulse", "candlelight"]
        n = 0
        while not self.stop.is_set():
            n += 1
            r = random.random()
            if n % 15 == 0:
                body, path = {}, "/scenes/soak_scene/activate"
            elif r < 0.35:
                body, path = {"power": n % 2 == 0, "transitionMs": 0}, "/lights/soak_a/command"
            elif r < 0.65:
                body, path = {"brightness": random.randint(1, 100), "transitionMs": random.choice([0, 500, 1500])}, "/lights/soak_a/command"
            elif r < 0.8:
                body, path = {"effect": random.choice(effects), "effectParams": {"durationMs": 3000}}, "/lights/soak_b/command"
            else:
                body, path = {"power": False, "transitionMs": 800}, "/lights/soak_b/command"
            try:
                self.call(lambda L: L.post(path, body))
                with self.lock:
                    self.sent += 1
            except Exception:  # noqa: BLE001
                with self.lock:
                    self.errors += 1
            self.stop.wait(2.0)

    def websocket(self):
        import websockets

        async def run():
            while not self.stop.is_set():
                try:
                    if self.L is None:
                        self.session()
                    cookie = f"lumen_session={self.L.s.cookies.get('lumen_session')}"
                    async with websockets.connect(self.url.replace("http", "ws") + "/api/v1/ws", extra_headers={"Cookie": cookie}, ping_interval=20) as ws:
                        while not self.stop.is_set():
                            try:
                                await asyncio.wait_for(ws.recv(), 5)
                            except asyncio.TimeoutError:
                                pass
                except Exception:  # noqa: BLE001 — reconnect
                    await asyncio.sleep(5)

        asyncio.run(run())

    def run(self, hours):
        os.makedirs(self.out, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        path = os.path.join(self.out, f"soak-{stamp}.csv")
        self.setup()
        threads = [threading.Thread(target=self.load, daemon=True), threading.Thread(target=self.websocket, daemon=True)]
        for t in threads:
            t.start()
        start = time.time()
        last_uptime, reboots, first = None, 0, None
        rows = []
        with open(path, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=FIELDS)
            w.writeheader()
            while time.time() - start < hours * 3600:
                row = {"time": time.strftime("%Y-%m-%d %H:%M:%S"), "elapsed_h": round((time.time() - start) / 3600, 3), "reachable": 0}
                try:
                    d = self.call(lambda L: L.get("/system/diagnostics"))
                    mq = self.call(lambda L: L.get("/integrations"))["mqtt"]["status"]["connected"]
                    lg = d["lighting"]
                    up = d["uptimeSec"]
                    if last_uptime is not None and up < last_uptime:
                        reboots += 1
                    last_uptime = up
                    row.update(reachable=1, uptime_s=up, heap_free=d["heap"]["free"], heap_min=d["heap"]["minFree"],
                               heap_largest=d["heap"]["largestBlock"], ticks=lg["ticks"], tick_overruns=lg.get("tickOverruns"),
                               max_tick_us=lg["maxTickUs"], queue_dropped=lg["queueDropped"], hw_errors=lg["hardwareErrors"],
                               cmd_lat_avg_us=lg.get("commandLatencyUs", {}).get("avg"), cmd_lat_max_us=lg.get("commandLatencyUs", {}).get("max"),
                               ws_clients=d["web"]["wsClients"], ws_dropped=d["web"]["wsDropped"],
                               wifi_reconnects=d["network"]["wifi"]["reconnects"], rssi=d["network"]["wifi"]["rssi"], mqtt_connected=int(mq),
                               boot_count=d.get("bootCount"), reset_reason=d.get("resetReason"))
                    first = first or row
                except Exception:  # noqa: BLE001
                    pass
                with self.lock:
                    row.update(commands_sent=self.sent, command_errors=self.errors, reboots=reboots)
                w.writerow(row)
                f.flush()
                rows.append(row)
                self.stop.wait(60)
        self.stop.set()
        self.summary(path, rows, first, reboots)

    def summary(self, path, rows, first, reboots):
        ok = [r for r in rows if r["reachable"]]
        lines = [f"soak log: {path}", f"duration: {rows[-1]['elapsed_h']:.2f} h, samples {len(rows)}, reachable {len(ok)}"]
        if ok:
            heap = [r["heap_free"] for r in ok]
            lines += [
                f"reboots seen: {reboots} (uptime {first['uptime_s']} s -> {ok[-1]['uptime_s']} s; boot count {first.get('boot_count')} -> {ok[-1].get('boot_count')}, last reset reason {ok[-1].get('reset_reason')})",
                f"heap free: start {heap[0]} B, end {heap[-1]} B, min sample {min(heap)} B; min-ever {ok[-1]['heap_min']} B; largest block end {ok[-1]['heap_largest']} B",
                f"heap trend over the last half: {heap[len(heap) // 2]} -> {heap[-1]} B",
                f"lighting overruns: {ok[-1]['tick_overruns']} of {ok[-1]['ticks']} ticks, worst tick {ok[-1]['max_tick_us'] / 1000:.1f} ms",
                f"queue dropped {ok[-1]['queue_dropped']}, hardware errors {ok[-1]['hw_errors']}, WS dropped {ok[-1]['ws_dropped']}",
                f"command latency on device: avg {ok[-1]['cmd_lat_avg_us']} us, max {ok[-1]['cmd_lat_max_us']} us",
                f"commands sent {rows[-1]['commands_sent']}, failed {rows[-1]['command_errors']} (includes PC-side network outages)",
            ]
        text = "\n".join(lines)
        print(text)
        with open(path.replace(".csv", "-summary.txt"), "w") as f:
            f.write(text + "\n")
        try:
            L = self.session()
            for p in ("/automations/soak_echo", "/scenes/soak_scene", "/lights/soak_a", "/lights/soak_b"):
                try:
                    L.delete(p)
                except Exception:  # noqa: BLE001
                    pass
        except Exception:  # noqa: BLE001
            pass


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("password")
    ap.add_argument("--hours", type=float, default=24)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "soak", "results"))
    a = ap.parse_args()
    Soak(a.url, a.password, a.out).run(a.hours)
