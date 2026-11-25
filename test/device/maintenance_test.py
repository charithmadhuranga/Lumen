"""On-device maintenance drills: OTA (valid, corrupt, truncated, wrong file,
crash and health-check rollback), configuration backup/restore and web UI
updates. Destructive to the *firmware slots*, never to NVS secrets.

    python3 maintenance_test.py <url> <admin password> <image dir> [drill ...]

<image dir> must contain (build with LUMEN_VERSION / LUMEN_OTA_SELFTEST, see
scripts/pio_version.py):  ok.bin  crash.bin  unhealthy.bin
Drills: invalid valid crash unhealthy backup ui   (default: all, in order)
"""
import json
import os
import socket
import sys
import time

import requests

from lumen_client import ApiError, Lumen

URL, PASSWORD, IMAGES = sys.argv[1].rstrip("/"), sys.argv[2], sys.argv[3]
HOST = URL.split("//", 1)[1].split("/")[0]
DRILLS = sys.argv[4:] or ["invalid", "valid", "crash", "unhealthy", "backup", "ui"]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def session():
    L = Lumen(URL)
    L.login("admin", PASSWORD)
    return L


def upload(L, path, data, timeout=120):
    r = L.s.post(URL + "/api/v1" + path, data=data, timeout=timeout,
                 headers={"Content-Type": "application/octet-stream", "X-CSRF-Token": L.csrf})
    try:
        return r.status_code, r.json()
    except ValueError:
        return r.status_code, {"raw": r.text[:200]}


def wait_back(timeout=90, gone_first=True):
    t = time.time()
    if gone_first:
        while time.time() - t < 15:
            try:
                requests.get(URL + "/api/v1/system/health", timeout=1)
            except requests.RequestException:
                break
            time.sleep(0.5)
    while time.time() - t < timeout:
        try:
            if requests.get(URL + "/api/v1/system/health", timeout=2).ok:
                time.sleep(2)
                return session()
        except requests.RequestException:
            pass
        time.sleep(1)
    raise AssertionError("controller did not come back")


def status(L):
    return L.get("/ota/status")


def config_fingerprint(L):
    return (sorted(x["id"] for x in L.get("/lights")), sorted(x["id"] for x in L.get("/rooms")),
            sorted(x["id"] for x in L.get("/scenes")))


def check(cond, msg):
    print(("PASS  " if cond else "FAIL  ") + msg, flush=True)
    if not cond:
        raise SystemExit(1)


def img(name):
    return open(os.path.join(IMAGES, name), "rb").read()


def drill_invalid(L):
    before = status(L)
    ok = img("ok.bin")
    pack = open(os.path.join(ROOT, "data", "web", "web.pack"), "rb").read()
    code, r = upload(L, "/ota/upload", pack[:200000])
    check(code == 400 and "Not an ESP32 firmware" in r["error"]["message"], f"web.pack rejected as firmware: {r['error']['message']}")
    code, r = upload(L, "/ota/upload", os.urandom(4096))
    check(code == 400, f"random bytes rejected: {r['error']['message']}")
    bad = bytearray(ok)
    bad[len(bad) // 2] ^= 0xFF
    code, r = upload(L, "/ota/upload", bytes(bad))
    check(code == 400 and "corrupted" in r["error"]["message"], f"corrupted image rejected after full verify: {r['error']['message']}")
    code, r = upload(L, "/ota/upload", ok[:100])
    check(code == 400, "header-only image rejected")
    # Truncated upload: announce the full size, send 40 %, drop the connection.
    s = socket.create_connection((HOST, 80), timeout=10)
    req = (f"POST /api/v1/ota/upload HTTP/1.1\r\nHost: {HOST}\r\nContent-Type: application/octet-stream\r\n"
           f"Content-Length: {len(ok)}\r\nX-CSRF-Token: {L.csrf}\r\nCookie: lumen_session={L.s.cookies.get('lumen_session')}\r\n\r\n")
    s.sendall(req.encode() + ok[: len(ok) * 2 // 5])
    s.close()
    time.sleep(35)  # httpd recv timeout x retries
    st = status(L)
    check(st["state"] == "error" and "interrupted" in st["error"], f"interrupted upload aborted: {st['error']}")
    code, r = L.s.post(URL + "/api/v1/ota/upload", data=b"x" * 10, timeout=10,
                       headers={"Content-Type": "application/octet-stream"}).status_code, None
    check(code == 403, "upload without CSRF token refused")
    code = requests.post(URL + "/api/v1/ota/upload", data=ok[:1000], timeout=10).status_code
    check(code == 401, "upload without session refused")
    after = status(L)
    check(after["running"] == before["running"], "running firmware unchanged by all invalid uploads")


def drill_valid(L):
    fp = config_fingerprint(L)
    before = status(L)
    t = time.time()
    code, r = upload(L, "/ota/upload", img("ok.bin"))
    check(code == 200 and r["data"]["restarting"], f"valid image accepted ({time.time() - t:.1f}s, {r['data']['newVersion']})")
    L = wait_back()
    st = status(L)
    check(st["running"]["partition"] != before["running"]["partition"], f"booted the other slot ({st['running']['partition']})")
    check(st["running"]["version"] == r["data"]["newVersion"], f"running {st['running']['version']}")
    check(st["running"]["state"] == "pending_verify", "new firmware starts pending verification")
    time.sleep(25)
    st = status(L)
    check(st["running"]["state"] == "valid", "new firmware confirmed itself after the health check")
    check(st["other"]["version"] == before["running"]["version"], "previous firmware kept in the other slot")
    check(config_fingerprint(L) == fp, "configuration preserved across the update")
    return L


def drill_rollback(L, image, wait_s, label):
    before = status(L)
    fp = config_fingerprint(L)
    code, r = upload(L, "/ota/upload", img(image))
    check(code == 200, f"{label} image accepted ({r['data']['newVersion']})")
    t = time.time()
    seen_new = False
    while time.time() - t < wait_s:
        try:
            L2 = session()
            v = status(L2)["running"]["version"]
            if v == r["data"]["newVersion"]:
                seen_new = True
            elif seen_new and v == before["running"]["version"]:
                break
        except (requests.RequestException, ApiError, KeyError):
            pass
        time.sleep(2)
    L = wait_back(gone_first=False)
    st = status(L)
    # "aborted"/"invalid" is only set for an image that booted pending-verify
    # and never confirmed itself, so it proves the bad image really ran.
    print(f"      (bad image {'seen' if seen_new else 'not seen'} over HTTP while it ran)")
    check(st["other"]["version"] == r["data"]["newVersion"], f"{label} image is the one in the other slot")
    check(st["running"]["version"] == before["running"]["version"] and st["running"]["partition"] == before["running"]["partition"],
          f"{label}: rolled back to {st['running']['version']} on {st['running']['partition']}")
    check(st["other"]["state"] in ("invalid", "aborted"), f"{label} image marked {st['other']['state']}")
    check(config_fingerprint(L) == fp, f"{label}: configuration intact")
    return L


def drill_backup(L):
    full = L.get("/backup")
    check(full["format"] == "lumen-backup" and "lights.json" in full["documents"], f"backup has {len(full['documents'])} documents")
    blob = json.dumps(full)
    for s in (PASSWORD,):
        check(s not in blob, "no admin password in the backup")
    fp = config_fingerprint(L)
    L.post("/scenes/capture", {"id": "drill_scene", "name": "Drill scene"})
    check("drill_scene" in config_fingerprint(L)[2], "scene added after the backup")
    for bad, why in ((b"{}", "empty object"), (b'{"format":"lumen-backup","version":9,"documents":{"a.json":{}}}', "future version"),
                     (b'{"format":"lumen-backup","version":1,"documents":{"../x.json":{}}}', "path traversal")):
        code, r = upload(L, "/backup/restore", bad)
        check(code == 400, f"restore refuses {why}: {r['error']['message']}")
    check("drill_scene" in config_fingerprint(L)[2], "failed restores changed nothing")
    code, r = upload(L, "/backup/restore", json.dumps(full).encode())
    check(code == 200 and r["data"]["restarting"], f"restore accepted ({len(r['data']['restored'])} documents)")
    L = wait_back()
    check(config_fingerprint(L) == fp, "configuration equals the backup after restart (drill scene gone)")
    # Partial (merge) restore of only scenes.json keeps everything else.
    L.post("/scenes/capture", {"id": "drill_scene", "name": "Drill scene"})
    part = L.get("/backup?sections=scenes")
    L.delete("/scenes/drill_scene")
    code, r = upload(L, "/backup/restore?mode=merge", json.dumps(part).encode())
    check(code == 200 and r["data"]["restored"] == ["scenes.json"], "partial restore accepted")
    L = wait_back()
    fp2 = config_fingerprint(L)
    check("drill_scene" in fp2[2] and fp2[0] == fp[0] and fp2[1] == fp[1], "partial restore brought the scene back and kept lights/rooms")
    L.delete("/scenes/drill_scene")
    return L


def drill_ui(L):
    pack = open(os.path.join(ROOT, "data", "web", "web.pack"), "rb").read()
    code, r = upload(L, "/system/ui/upload", b"NOPE" + pack[4:])
    check(code == 400, f"non-pack rejected before touching the UI: {r['error']['message']}")
    check(requests.get(URL + "/", timeout=5).text.find("Lumen recovery") < 0, "UI still installed after the rejected upload")
    code, r = upload(L, "/system/ui/upload", pack[: len(pack) // 2])
    check(code == 400, f"truncated pack rejected: {r['error']['message']}")
    home = requests.get(URL + "/", timeout=5).text
    check("Lumen recovery" in home or "<div id=\"root\"" in home, "after a failed UI update '/' serves the recovery page or the old UI")
    code, r = upload(L, "/system/ui/upload", pack)
    check(code == 200 and r["data"]["assets"] > 10, f"web UI installed ({r['data']['assets']} assets)")
    home = requests.get(URL + "/", timeout=5)
    check(home.ok and "Lumen recovery" not in home.text, "UI served again")
    check(requests.get(URL + "/recovery", timeout=5).ok, "/recovery always available")
    return L


if __name__ == "__main__":
    L = session()
    for d in DRILLS:
        print(f"--- {d}", flush=True)
        if d == "invalid":
            drill_invalid(L)
        elif d == "valid":
            L = drill_valid(L)
        elif d == "crash":
            L = drill_rollback(L, "crash.bin", 90, "crash")
        elif d == "unhealthy":
            L = drill_rollback(L, "unhealthy.bin", 200, "unhealthy")
        elif d == "backup":
            L = drill_backup(L)
        elif d == "ui":
            L = drill_ui(L)
    print("all drills passed")
