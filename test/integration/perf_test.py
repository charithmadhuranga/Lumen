"""Performance measurements against the real controller (SPEC §24, Phase 12.6).

    python3 perf_test.py http://<controller-ip> '<admin password>' [--mqtt host:port:user:pass] [-n 40]

Targets: local command < 100 ms (API accept -> hardware), UI propagation
< 250 ms (command -> WebSocket light_state_changed), transitions >= 30
updates/s, automation trigger < 100 ms. Reports median / p95 / max.
Uses PCA9685 channel 11 (creates and deletes light "perf_lamp").
"""
import argparse
import asyncio
import json
import statistics
import subprocess
import sys
import time

import requests

sys.path.insert(0, __file__.rsplit("/", 2)[0] + "/device")
from lumen_client import Lumen  # noqa: E402

CH = "pca9685_0:11"


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(p / 100 * (len(xs) - 1))))]


def report(name, ms, target=None):
    med, p95, mx = statistics.median(ms), pct(ms, 95), max(ms)
    ok = target is None or p95 <= target
    print(f"{'PASS' if ok else 'FAIL'}  {name}: median {med:.0f} ms · p95 {p95:.0f} ms · max {mx:.0f} ms (n={len(ms)}{f', target p95 < {target} ms' if target else ''})")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("password")
    ap.add_argument("-n", type=int, default=40)
    a = ap.parse_args()
    url = a.url.rstrip("/")
    L = Lumen(url)
    L.login("admin", a.password)
    try:
        L.delete("/lights/perf_lamp")
    except Exception:  # noqa: BLE001
        pass
    L.post("/lights", {"id": "perf_lamp", "name": "Perf lamp", "type": "dimmer", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 11}, "transitionMs": 0})
    rb = lambda: L.get(f"/hardware/outputs/{CH}/readback")["percent"]  # noqa: E731
    results = []

    # 1. HTTP command accepted (202) round trip.
    http = []
    for i in range(a.n):
        t = time.perf_counter()
        L.post("/lights/perf_lamp/command", {"power": i % 2 == 0, "brightness": 100, "transitionMs": 0})
        http.append((time.perf_counter() - t) * 1000)
    report("HTTP command round trip over Wi-Fi (informational; the SPEC target is the on-device latency below)", http)

    # 2. Command -> hardware: poll the PCA register until it changes. The
    #    read-back request itself costs one round trip, which is included.
    hw = []
    for i in range(min(a.n, 20)):
        target = 100 if i % 2 == 0 else 0
        t = time.perf_counter()
        L.post("/lights/perf_lamp/command", {"power": target == 100, "brightness": 100, "transitionMs": 0})
        while rb() != target:
            if time.perf_counter() - t > 2:
                break
        hw.append((time.perf_counter() - t) * 1000)
    report("HTTP command -> PCA9685 register (incl. read-back request, informational)", hw)

    # 3. Command -> WebSocket event (what the UI sees).
    import websockets

    async def ws_latency():
        cookie = f"lumen_session={L.s.cookies.get('lumen_session')}"
        out = []
        async with websockets.connect(url.replace("http", "ws") + "/api/v1/ws", extra_headers={"Cookie": cookie}) as ws:
            await asyncio.sleep(0.5)
            for i in range(a.n):
                want = i % 2 == 0
                t = time.perf_counter()
                await asyncio.to_thread(L.post, "/lights/perf_lamp/command", {"power": want, "brightness": 100, "transitionMs": 0})
                while True:
                    msg = json.loads(await asyncio.wait_for(ws.recv(), 3))
                    if msg.get("event") == "light_state_changed" and msg["data"].get("channelId") == "perf_lamp" and msg["data"]["output"]["power"] == want:
                        break
                out.append((time.perf_counter() - t) * 1000)
                await asyncio.sleep(0.05)
        return out

    results.append(report("HTTP command -> WebSocket light_state_changed", asyncio.run(ws_latency()), 250))

    # 4. Transition update rate: a 2 s fade sampled through read-back.
    L.post("/lights/perf_lamp/command", {"power": True, "brightness": 1, "transitionMs": 0})
    time.sleep(0.3)
    d0 = L.get("/system/diagnostics")["lighting"]
    L.post("/lights/perf_lamp/command", {"power": True, "brightness": 100, "transitionMs": 2000})
    samples, t = [], time.perf_counter()
    while time.perf_counter() - t < 2.2:
        samples.append(rb())
    d1 = L.get("/system/diagnostics")["lighting"]
    distinct = len(set(samples))
    writes = d1["hardwareWrites"] - d0["hardwareWrites"]
    rate = writes / 2.2
    ok = rate >= 30
    print(f"{'PASS' if ok else 'FAIL'}  transition: {writes} hardware writes in ~2.2 s (~{rate:.0f}/s, target >= 30/s); {distinct} distinct levels seen by {len(samples)} read-backs; max tick {d1['maxTickUs'] / 1000:.1f} ms")
    results.append(ok)

    # 5. Lighting loop timing under the load above.
    ticks = d1["ticks"]
    print(f"INFO  lighting loop: {ticks} ticks since boot, worst tick {d1['maxTickUs'] / 1000:.1f} ms (period 20 ms), queue dropped {d1['queueDropped']}")

    # 6. Automation trigger: light change -> automation sets another light.
    try:
        L.delete("/lights/perf_echo")
    except Exception:  # noqa: BLE001
        pass
    L.post("/lights", {"id": "perf_echo", "name": "Perf echo", "type": "dimmer", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 10}, "transitionMs": 0})
    auto = {"id": "perf_auto", "name": "Perf echo", "enabled": True,
            "trigger": {"type": "light_state", "light_id": "perf_lamp", "state": True},
            "actions": [{"type": "light_brightness", "light_id": "perf_echo", "brightness": 100, "transition_ms": 0}],
            "execution": {"cooldown_ms": 0, "max_executions_per_hour": 1000}}
    try:
        L.delete("/automations/perf_auto")
    except Exception:  # noqa: BLE001
        pass
    created = True
    try:
        L.post("/automations", auto)
    except Exception as e:  # noqa: BLE001
        created = False
        print(f"SKIP  automation trigger: could not create the test automation ({e})")
    if created:
        lat = []
        for _ in range(10):
            L.post("/lights/perf_lamp/command", {"power": False, "transitionMs": 0})
            L.post("/lights/perf_echo/command", {"power": False, "transitionMs": 0})
            time.sleep(0.4)
            t = time.perf_counter()
            L.post("/lights/perf_lamp/command", {"power": True, "brightness": 100, "transitionMs": 0})
            while L.get("/hardware/outputs/pca9685_0:10/readback")["percent"] != 100:
                if time.perf_counter() - t > 3:
                    break
            lat.append((time.perf_counter() - t) * 1000)
        # Subtract one command round trip + one read-back (measured above).
        base = statistics.median(http) + statistics.median(hw) / 2
        net = [max(0.0, x - base) for x in lat]
        report("automation end-to-end over Wi-Fi, network overhead estimated (informational)", net)
        print(f"      raw end-to-end incl. HTTP: median {statistics.median(lat):.0f} ms")
        L.delete("/automations/perf_auto")
    L.delete("/lights/perf_echo")
    L.delete("/lights/perf_lamp")
    # On-device latency (controller's own clock; excludes Wi-Fi / HTTP).
    lg = L.get("/system/diagnostics")["lighting"]
    cl = lg.get("commandLatencyUs")
    if cl:
        ok = cl["max"] < 100_000
        print(f"{'PASS' if ok else 'FAIL'}  on-device command latency (enqueue -> hardware): avg {cl['avg'] / 1000:.1f} ms · max {cl['max'] / 1000:.1f} ms over {cl['count']} commands (target < 100 ms)")
        results.append(ok)
        aw = lg.get("automationEventWaitUs", {})
        worst = (aw.get("max", 0) + cl["max"]) / 1000
        ok = worst < 100
        print(f"{'PASS' if ok else 'FAIL'}  on-device automation trigger (event wait max {aw.get('max', 0) / 1000:.1f} ms + command max {cl['max'] / 1000:.1f} ms = {worst:.1f} ms worst case, target < 100 ms)")
        results.append(ok)
        print(f"INFO  lighting loop overruns (> 20 ms): {lg.get('tickOverruns')} of {lg['ticks']} ticks")
    heap = L.get("/system/diagnostics")["heap"]
    print(f"INFO  heap after run: free {heap['free']} B, minimum ever {heap['minFree']} B, largest block {heap['largestBlock']} B")
    print(f"\n{sum(results)}/{len(results)} targets met")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
