"""On-device reliability suite (SPEC §27 reliability, release gate).

    python3 reliability_test.py http://<controller> '<password>' [--peer http://<B>] [--channel 12]

Needs a TEST IMAGE on the controller (fault-injection routes under
/api/v1/test/*, never in release builds):

    LUMEN_TEST_HOOKS=1 LUMEN_VERSION=0.0.0-reliability pio run -e esp32
    python3 test/device/ota_install.py '<password>' .pio/build/esp32/firmware.bin http://<controller>

Re-install the normal image afterwards. Sections:
  1. PCA9685 stops answering (I2C fault injected in the bus driver): other
     work continues, health reports it, no tick storm; on return the chip is
     reconfigured and gets the latest state.
  2. PCA9685 power-cycle (chip put to sleep behind the driver's back): the
     periodic MODE1 check wakes it and rewrites the outputs.
  3. Low memory (heap reserved down to ~12 KB): no reboot, lighting keeps
     ticking and applying commands, the API degrades and recovers.
  4. ESP-NOW peer loss and return (with --peer): offline detection, clear
     409 for remote commands, local lights unaffected, automatic rejoin.
  5. ESP-NOW "local wins" (with --peer): a remote command that arrives after
     a newer local command is rejected as stale.
Creates the light "rel_pca" on pca9685_0:<channel> and removes it again.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "device"))
from lumen_client import ApiError, Lumen  # noqa: E402

RESULTS = []
PCA_ADDR = 0x40


def check(cond, msg):
    RESULTS.append(bool(cond))
    print(("PASS  " if cond else "FAIL  ") + msg, flush=True)
    return cond


def until(fn, timeout=5.0, interval=0.1):
    t = time.perf_counter()
    while time.perf_counter() - t < timeout:
        try:
            if fn():
                return time.perf_counter() - t
        except Exception:  # noqa: BLE001
            pass
        time.sleep(interval)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("password")
    ap.add_argument("--peer", help="second controller for the ESP-NOW sections")
    ap.add_argument("--channel", type=int, default=12, help="free PCA9685 channel for the test light")
    x = ap.parse_args()
    A = Lumen(x.url, timeout=20)
    A.login("admin", x.password)
    diag = lambda: A.get("/system/diagnostics")  # noqa: E731
    d0 = diag()
    if not d0.get("testHooks"):
        print("This controller runs a release image: flash a LUMEN_TEST_HOOKS=1 image (see the docstring).")
        sys.exit(2)
    boot0 = d0["bootCount"]
    out = f"pca9685_0:{x.channel}"
    pca = lambda: next(p for p in diag()["peripherals"] if p["id"] == "pca9685_0")  # noqa: E731
    readback = lambda: A.get(f"/hardware/outputs/{out}/readback")["percent"]  # noqa: E731
    output = lambda: A.get("/lights/rel_pca")["output"]  # noqa: E731
    lighting = lambda: diag()["lighting"]  # noqa: E731

    try:
        A.delete("/lights/rel_pca")
    except ApiError:
        pass
    A.post("/lights", {"id": "rel_pca", "name": "Reliability", "type": "dimmer",
                       "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": x.channel}, "transitionMs": 0})
    # Readback is the raw PWM duty (after the perceptual curve), so record the
    # duty each brightness produces while the chip is healthy.
    duty = {}
    for level in (70, 55, 35, 15, 45, 40):
        A.post("/lights/rel_pca/command", {"power": True, "brightness": level, "transitionMs": 0})
        until(lambda: output()["brightness"] == level, 3)
        time.sleep(0.2)
        duty[level] = round(readback(), 2)
    at = lambda level: round(readback(), 2) == duty[level]  # noqa: E731
    check(pca()["ok"] and len(set(duty.values())) == len(duty), f"baseline: PCA9685 healthy, duty per brightness {duty}")

    # 1. PCA9685 stops answering.
    try:
        l0 = lighting()
        A.post("/test/i2c/fault", {"address": PCA_ADDR, "fail": True})
        A.post("/lights/rel_pca/command", {"brightness": 25, "transitionMs": 0})
        time.sleep(0.3)
        A.post("/lights/rel_pca/command", {"brightness": 70, "transitionMs": 0})  # latest state
        ok = until(lambda: not pca()["ok"], 3) is not None
        h = pca()
        check(ok and h["detail"] == "not responding", f"fault reported in diagnostics: {h['detail']!r}, errors {h['errors']}")
        time.sleep(3)
        l1 = lighting()
        rate = (l1["ticks"] - l0["ticks"]) / 3.3
        check(rate > 30 and l1["tickOverruns"] - l0["tickOverruns"] <= 5,
              f"lighting loop keeps ticking during the fault ({rate:.0f} ticks/s, +{l1['tickOverruns'] - l0['tickOverruns']} overruns)")
        check(output()["brightness"] == 70, "engine accepted the latest command during the fault (applied state 70 %)")
        try:
            readback()
            check(False, "readback should fail while the chip is unreachable")
        except ApiError as e:
            check(True, f"readback reports the fault ({e.status} {e.code})")
    finally:
        A.post("/test/i2c/fault", {"address": PCA_ADDR, "fail": False})
    t = until(lambda: at(70) and pca()["ok"], 5)
    check(t is not None, f"chip back: reconfigured and holds the latest state (70 %) after {t or 0:.1f} s")
    check("recovered" in pca()["detail"], f"recovery counted: {pca()['detail']!r}")

    # 2. Power-cycle: put the chip to sleep (as after a power loss) behind the driver's back.
    A.post("/test/i2c/write", {"address": PCA_ADDR, "reg": 0x00, "value": 0x10})
    asleep = A.post("/test/i2c/read", {"address": PCA_ADDR, "reg": 0x00})["value"] & 0x10
    t = until(lambda: (A.post("/test/i2c/read", {"address": PCA_ADDR, "reg": 0x00})["value"] & 0x10) == 0, 8, 0.25)
    check(asleep and t is not None, f"sleeping chip detected and woken within {t or 0:.1f} s (check interval ~5 s)")
    check(at(70), "outputs rewritten after the wake-up (70 %)")

    # 3. Low memory.
    #    Large responses (diagnostics) may be refused while memory is short;
    #    that is the intended degradation, so measure before and after.
    l0 = lighting()
    r = A.post("/test/heap/reserve", {"leaveBytes": 12288})
    print(f"INFO  reserved {r['heapReserved'] // 1024} KB, free heap now {r['freeHeap']} B")
    applied, failed, last = 0, 0, None
    t0 = time.time()
    try:
        for i in range(20):
            level = (55, 35, 15, 45)[i % 4]
            try:
                A.post("/lights/rel_pca/command", {"brightness": level, "transitionMs": 0})
                applied += 1
                last = level
            except Exception:  # noqa: BLE001
                failed += 1
            time.sleep(0.15)
        reached = last is not None and until(lambda: at(last), 3) is not None
        try:
            A.get("/system/diagnostics")
            print("INFO  diagnostics still served under memory pressure")
        except ApiError as e:
            print(f"INFO  diagnostics refused under memory pressure ({e.status} {e.message})")
        check(applied > 0 and reached, f"low memory: {applied}/20 commands accepted ({failed} refused), last one ({last} %) reached the PCA9685")
    finally:
        try:
            A.post("/test/heap/release", {})
        except Exception:  # noqa: BLE001
            time.sleep(2)
            A.login("admin", x.password)
            A.post("/test/heap/release", {})
    d1 = diag()
    l1 = d1["lighting"]
    rate = (l1["ticks"] - l0["ticks"]) / (time.time() - t0 + 1)
    check(rate > 30, f"low memory: lighting kept ticking ({rate:.0f} ticks/s over the episode)")
    check(d1["bootCount"] == boot0 and d1["uptimeSec"] >= d0["uptimeSec"], f"no reboot under memory pressure (boot count {d1['bootCount']}, heap min {d1['heap']['minFree']} B)")
    A.post("/lights/rel_pca/command", {"brightness": 55, "transitionMs": 0})
    check(until(lambda: at(55), 3) is not None and d1["heap"]["free"] > 40000, f"after release everything works again (free {d1['heap']['free']} B)")
    print(f"INFO  failed allocations recorded: {d1['allocFailures']['total']} {d1['allocFailures']['recent'][-2:]}")

    # 4./5. ESP-NOW.
    if x.peer:
        B = Lumen(x.peer, timeout=20)
        B.login("admin", x.password)
        aid, bid = A.get("/controllers")["self"]["id"], B.get("/controllers")["self"]["id"]
        key = A.get("/integrations/espnow/key")["key"]
        net = A.get("/controllers")["self"]["networkId"]
        B.put("/integrations/espnow", {"enabled": True, "networkId": net, "key": key})
        peer = lambda L, pid: next((p for p in L.get("/controllers")["peers"] if p["id"] == pid), None)  # noqa: E731
        check(until(lambda: (peer(A, bid) or {}).get("online") and any(l["id"] == "rel_pca" for l in peer(B, aid)["lights"]), 40, 1) is not None,
              f"{aid} and {bid} see each other")
        b_light = B.get("/lights")[0]["id"]

        # 4. Peer loss: B's radio goes away.
        B.put("/integrations/espnow", {"enabled": False})
        t = until(lambda: not (peer(A, bid) or {}).get("online", False), 40, 1)
        check(t is not None, f"{aid} marks {bid} offline after {t or 0:.0f} s (timeout 20 s)")
        try:
            A.post(f"/controllers/{bid}/lights/{b_light}/command", {"power": True})
            check(False, "remote command to an offline peer must be refused")
        except ApiError as e:
            check(e.status == 409, f"remote command to the offline peer refused clearly ({e.status} {e.code})")
        A.post("/lights/rel_pca/command", {"brightness": 35, "transitionMs": 0})
        check(until(lambda: at(35), 3) is not None, "local lights unaffected by the peer loss")
        B.put("/integrations/espnow", {"enabled": True, "networkId": net, "key": key})
        t = until(lambda: (peer(A, bid) or {}).get("online"), 30, 1)
        check(t is not None, f"{bid} rejoins automatically after {t or 0:.0f} s")
        until(lambda: any(l["id"] == "rel_pca" for l in (peer(B, aid) or {"lights": []})["lights"]), 35, 1)

        # 5. Local wins: hold A's ESP-NOW receive path, send a remote command
        #    from B, then a newer local command on A; the late remote one is stale.
        st0 = A.get("/controllers")["stats"]
        A.post("/test/espnow/hold", {"ms": 2500})
        try:
            B.post(f"/controllers/{aid}/lights/rel_pca/command", {"brightness": 90, "transitionMs": 0})
            time.sleep(1.2)
            A.post("/lights/rel_pca/command", {"brightness": 15, "transitionMs": 0})  # local, newer
            time.sleep(5)
        finally:
            A.post("/test/espnow/hold", {"ms": 0})
        st1 = A.get("/controllers")["stats"]
        check(at(15) and st1["commandsRejected"] > st0["commandsRejected"],
              f"late remote command rejected as stale, local command kept (15 %; rejected +{st1['commandsRejected'] - st0['commandsRejected']})")
        B.post(f"/controllers/{aid}/lights/rel_pca/command", {"brightness": 45, "transitionMs": 0})
        check(until(lambda: at(45), 3) is not None, "a fresh remote command is accepted again")
    else:
        print("SKIP  ESP-NOW peer loss / local-wins (no --peer)")

    A.delete("/lights/rel_pca")
    print(f"\n{sum(RESULTS)}/{len(RESULTS)} passed")
    sys.exit(0 if all(RESULTS) else 1)


if __name__ == "__main__":
    main()
