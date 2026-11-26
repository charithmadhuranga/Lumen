"""Two-controller ESP-NOW tests on real hardware (Phase 11 gate).

    python3 multi_test.py http://<controller A> http://<controller B> '<password>' --a-light ceiling --a-output pca9685_0:0 --b-light desk_led

A must have a PCA9685 light (verified by register read-back); B any light.
Both must share network id + key (the test sets them). Uses scene "net_scene".
"""
import argparse
import statistics
import subprocess
import sys
import time
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "device"))
from lumen_client import ApiError, Lumen  # noqa: E402

RESULTS = []


def check(cond, msg):
    RESULTS.append(bool(cond))
    print(("PASS  " if cond else "FAIL  ") + msg, flush=True)
    return cond


def until(fn, timeout=3.0, interval=0.05):
    t = time.perf_counter()
    while time.perf_counter() - t < timeout:
        try:
            v = fn()
            if v:
                return (time.perf_counter() - t) * 1000
        except Exception:  # noqa: BLE001
            pass
        time.sleep(interval)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("password")
    ap.add_argument("--a-light", default="ceiling")
    ap.add_argument("--a-output", default="pca9685_0:0")
    ap.add_argument("--b-light", default="desk_led")
    x = ap.parse_args()
    A, B = Lumen(x.a), Lumen(x.b)
    A.login("admin", x.password)
    B.login("admin", x.password)
    aid, bid = A.get("/controllers")["self"]["id"], B.get("/controllers")["self"]["id"]

    # 0. Same network on both.
    A.put("/integrations/espnow", {"enabled": True, "networkId": "home"})
    key = A.get("/integrations/espnow/key")["key"]
    B.put("/integrations/espnow", {"enabled": True, "networkId": "home", "key": key})
    peer = lambda L, pid: next((p for p in L.get("/controllers")["peers"] if p["id"] == pid), None)  # noqa: E731
    check(until(lambda: (peer(A, bid) or {}).get("online") and (peer(B, aid) or {}).get("online"), 20) is not None, f"{aid} and {bid} discover each other")
    check(until(lambda: any(l["id"] == x.a_light for l in peer(B, aid)["lights"]), 35) is not None, f"{bid} knows {aid}'s light {x.a_light}")
    check(until(lambda: any(l["id"] == x.b_light for l in peer(A, bid)["lights"]), 35) is not None, f"{aid} knows {bid}'s light {x.b_light}")

    rb = lambda: A.get(f"/hardware/outputs/{x.a_output}/readback")["percent"]  # noqa: E731
    b_power = lambda: B.get(f"/lights/{x.b_light}")["output"]["power"]  # noqa: E731

    # 1. Remote commands both ways (hardware-verified on A).
    lat_ab, lat_ba = [], []
    for i in range(10):
        on = i % 2 == 0
        t = time.perf_counter()
        A.post(f"/controllers/{bid}/lights/{x.b_light}/command", {"power": on})
        ms = until(lambda: b_power() == on, 3, 0.02)
        lat_ab.append(ms if ms is not None else 9999)
        level = 100 if on else 0
        B.post(f"/controllers/{aid}/lights/{x.a_light}/command", {"power": on, "brightness": 100, "transitionMs": 0})
        ms = until(lambda: rb() == level, 3, 0.02)
        lat_ba.append(ms if ms is not None else 9999)
    check(max(lat_ab) < 1000, f"{aid} -> {bid} remote command applied: median {statistics.median(lat_ab):.0f} ms, max {max(lat_ab):.0f} ms (incl. HTTP polling)")
    check(max(lat_ba) < 1000, f"{bid} -> {aid} remote command reaches the PCA9685: median {statistics.median(lat_ba):.0f} ms, max {max(lat_ba):.0f} ms")

    # 2. State sync: local change on A shows up in B's view.
    lat = []
    for level in (20, 80, 35, 90, 10):
        A.post(f"/lights/{x.a_light}/command", {"power": True, "brightness": level, "transitionMs": 0})
        ms = until(lambda: next(l for l in peer(B, aid)["lights"] if l["id"] == x.a_light)["output"]["brightness"] == level, 3, 0.02)
        lat.append(ms if ms is not None else 9999)
    check(max(lat) < 1000, f"state sync {aid} -> {bid}: median {statistics.median(lat):.0f} ms, max {max(lat):.0f} ms (target < 250 ms over ESP-NOW + HTTP polling)")

    # 3. Network-wide scene.
    for L, lid, body in ((A, x.a_light, {"power": True, "brightness": 100, "transitionMs": 0}), (B, x.b_light, {"power": True})):
        try:
            L.delete("/scenes/net_scene")
        except ApiError:
            pass
        L.post(f"/lights/{lid}/command", body)
        time.sleep(0.4)
        L.post("/scenes/capture", {"id": "net_scene", "name": "Network scene", "lightIds": [lid], "transitionMs": 0})
    A.post(f"/lights/{x.a_light}/command", {"power": False, "transitionMs": 0})
    B.post(f"/lights/{x.b_light}/command", {"power": False})
    time.sleep(0.5)
    A.post("/controllers/scenes/net_scene/activate", {})
    check(until(lambda: rb() == 100) is not None and until(lambda: b_power()) is not None, "network scene activated on both controllers")

    # 4. Wrong key / other network are rejected.
    before = A.get("/controllers")["stats"]
    B.put("/integrations/espnow", {"enabled": True, "networkId": "home", "key": "0" * 32})
    time.sleep(7)
    B.post(f"/controllers/{aid}/lights/{x.a_light}/command", {"power": False}) if (peer(B, aid) or {}).get("online") else None
    time.sleep(1)
    after = A.get("/controllers")["stats"]
    check(after["authFailed"] > before["authFailed"] and rb() == 100, f"frames with a wrong key rejected (authFailed +{after['authFailed'] - before['authFailed']}), light unchanged")
    B.put("/integrations/espnow", {"enabled": True, "networkId": "other", "key": key})
    time.sleep(7)
    after2 = A.get("/controllers")["stats"]
    check(after2["otherNetwork"] > after["otherNetwork"], f"frames of another network ignored (+{after2['otherNetwork'] - after['otherNetwork']})")
    B.put("/integrations/espnow", {"enabled": True, "networkId": "home", "key": key})
    check(until(lambda: (peer(A, bid) or {}).get("online") and (peer(B, aid) or {}).get("online"), 20) is not None, "back on the same network")

    # 5. MQTT broker down: ESP-NOW keeps working.
    compose = ["docker", "compose", "-f", os.path.join(os.path.dirname(os.path.abspath(__file__)), "ha", "docker-compose.yml")]
    stopped = subprocess.run(compose + ["stop", "mosquitto"], capture_output=True).returncode == 0
    try:
        A.post(f"/controllers/{bid}/lights/{x.b_light}/command", {"power": False})
        check(until(lambda: b_power() is False, 3) is not None, f"remote command works with the MQTT broker {'stopped' if stopped else '(not running)'}")
    finally:
        if stopped:
            subprocess.run(compose + ["start", "mosquitto"], capture_output=True)

    # 6. No storms: 20 s of commands both ways, then frame rates settle.
    s0a, s0b = A.get("/controllers")["stats"], B.get("/controllers")["stats"]
    t0 = time.time()
    n = 0
    while time.time() - t0 < 20:
        A.post(f"/controllers/{bid}/lights/{x.b_light}/command", {"toggle": True})
        B.post(f"/controllers/{aid}/lights/{x.a_light}/command", {"brightness": 10 + (n % 9) * 10, "transitionMs": 0})
        n += 1
    time.sleep(3)
    s1a, s1b = A.get("/controllers")["stats"], B.get("/controllers")["stats"]
    tx_rate = ((s1a["tx"] - s0a["tx"]) + (s1b["tx"] - s0b["tx"])) / (time.time() - t0)
    check(s1a["duplicates"] == s0a["duplicates"] and s1b["duplicates"] == s0b["duplicates"], f"{2 * n} remote commands: no duplicate frames")
    check(tx_rate < 2 * n / 20 * 2 + 5, f"no traffic amplification: {tx_rate:.1f} frames/s for {2 * n / 20:.1f} commands/s")
    applied = (s1a["commandsApplied"] - s0a["commandsApplied"]) + (s1b["commandsApplied"] - s0b["commandsApplied"])
    print(f"INFO  delivered {applied}/{2 * n} remote commands during the burst ({100 * applied / (2 * n):.0f} %; broadcast ESP-NOW is unacknowledged)")
    time.sleep(5)
    s2a = A.get("/controllers")["stats"]
    idle = (s2a["tx"] - s1a["tx"]) / 5
    check(idle < 3, f"idle traffic returns to heartbeats ({idle:.1f} frames/s from {aid})")

    # 7. Restart B: it is back before the 20 s offline timeout; A keeps it as a
    #    peer and accepts its commands (sequence continues across the reboot).
    up_before = B.get("/system/diagnostics")["uptimeSec"]
    B.post("/system/restart", {})
    time.sleep(6)
    def b_back():
        B.login("admin", x.password)
        return B.get("/system/diagnostics")["uptimeSec"] < up_before
    check(until(b_back, 60, 2) is not None, f"{bid} restarted")
    check(until(lambda: (peer(A, bid) or {}).get("online") and (peer(B, aid) or {}).get("online"), 30, 1) is not None, f"{bid} rejoined after restart")
    A.post(f"/lights/{x.a_light}/command", {"power": False, "transitionMs": 0})
    time.sleep(0.5)
    rej = A.get("/controllers")["stats"]["replayed"]
    B.post(f"/controllers/{aid}/lights/{x.a_light}/command", {"power": True, "brightness": 50, "transitionMs": 0})
    check(until(lambda: rb() > 0, 3) is not None and A.get("/controllers")["stats"]["replayed"] == rej,
          f"commands from the rebooted {bid} accepted (no replay rejection: boot count keeps seq increasing)")

    for L in (A, B):
        try:
            L.delete("/scenes/net_scene")
        except ApiError:
            pass
    A.post(f"/lights/{x.a_light}/command", {"power": False, "transitionMs": 0})
    print(f"\n{sum(RESULTS)}/{len(RESULTS)} passed")
    sys.exit(0 if all(RESULTS) else 1)


if __name__ == "__main__":
    main()
