"""Security suite against the real controller (Phase 12 gate 1).

    python3 security_test.py http://<controller-ip> '<admin password>' [--secrets s1,s2,...] [--skip-ratelimit]

Checks: every API route requires a session (except the public ones), every
mutating route requires the CSRF token, cookie flags and security headers,
session end on logout and on password change (other sessions and WebSocket
clients), no secrets in any GET response or error, request-size limits,
login rate limiting (last: it blocks this IP for up to 5 minutes, the suite
waits until sign-in works again).
"""
import argparse
import asyncio
import json
import re
import sys
import time

import requests

ROUTES = """
POST /auth/logout|PUT /auth/password|POST /auth/password
GET /scenes|GET /scenes/x|POST /scenes|POST /scenes/capture|PUT /scenes/x|DELETE /scenes/x|POST /scenes/x/activate|POST /scenes/x/duplicate
GET /boards|GET /boards/active|POST /boards/validate|POST /boards|POST /boards/apply|GET /boards/x|GET /boards/x/pins|POST /boards/x/duplicate|PUT /boards/x|DELETE /boards/x|POST /boards/x/apply
GET /hardware/status|GET /hardware/inputs|GET /hardware/outputs|GET /hardware/outputs/x/readback|POST /hardware/outputs/x/test|GET /hardware/i2c/scan
GET /lights|GET /lights/effects|GET /lights/x|POST /lights|PUT /lights/x|DELETE /lights/x|POST /lights/x/command
GET /integrations|PUT /integrations/mqtt|PUT /integrations/homeassistant|POST /integrations/mqtt/test|PUT /integrations/tcp|PUT /integrations/udp|GET /integrations/token|POST /integrations/token/regenerate
GET /automations|GET /automations/settings|PUT /automations/settings|GET /automations/history|GET /automations/variables|PUT /automations/variables/x|POST /automations|GET /automations/x|PUT /automations/x|DELETE /automations/x|POST /automations/x/enable|POST /automations/x/disable|POST /automations/x/test|POST /automations/x/cancel|GET /automations/x/history
GET /rooms|GET /rooms/x|POST /rooms|PUT /rooms/x|DELETE /rooms/x|POST /rooms/x/power|POST /rooms/x/brightness
GET /groups|GET /groups/x|POST /groups|PUT /groups/x|DELETE /groups/x|POST /groups/x/command|POST /groups/x/power|POST /groups/x/brightness
GET /system/info|GET /system/diagnostics|POST /system/restart|POST /system/factory-reset
GET /network|GET /network/status|GET /network/scan|GET /network/ap-password|PUT /network/wifi|PUT /network/ap|PUT /network/ethernet|PUT /network/system
GET /ota/status|POST /ota/upload|POST /system/ui/upload|GET /backup|POST /backup/restore
GET /floorplans|POST /floorplans|GET /floorplans/x|PUT /floorplans/x|DELETE /floorplans/x|POST /floorplans/x/upload|GET /floorplans/x/svg|PUT /floorplans/x/layout
"""
ROUTES = [tuple(r.split(" ", 1)) for line in ROUTES.split("\n") for r in line.split("|") if r.strip()]
PUBLIC = [("POST", "/auth/login"), ("GET", "/auth/session"), ("GET", "/system/health")]
# GET routes whose purpose is to reveal one secret to the signed-in user.
REVEALS = {"/network/ap-password", "/integrations/token"}
SLOW_GET = {"/network/scan", "/hardware/i2c/scan", "/backup"}

RESULTS = []


def check(cond, msg):
    RESULTS.append(bool(cond))
    print(("PASS  " if cond else "FAIL  ") + msg, flush=True)
    return cond


class Session:
    def __init__(self, url, password):
        self.url = url
        self.s = requests.Session()
        r = self.s.post(url + "/api/v1/auth/login", json={"username": "admin", "password": password}, timeout=15)
        r.raise_for_status()
        self.login_response = r
        self.csrf = r.json()["data"]["csrfToken"]

    def call(self, method, path, body=None, csrf=True, **kw):
        h = {"X-CSRF-Token": self.csrf} if csrf and method != "GET" else {}
        return self.s.request(method, self.url + "/api/v1" + path, json=body if body is not None else ({} if method != "GET" else None),
                              headers=h, timeout=kw.pop("timeout", 15), **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("password")
    ap.add_argument("--secrets", default="", help="comma-separated secrets that must never appear (Wi-Fi, MQTT, …)")
    ap.add_argument("--skip-ratelimit", action="store_true")
    a = ap.parse_args()
    url = a.url.rstrip("/")
    secrets = [s for s in a.secrets.split(",") if s] + [a.password]

    # 1. Authentication required everywhere except the public routes.
    bad = []
    for m, p in ROUTES:
        r = requests.request(m, url + "/api/v1" + p, json={} if m != "GET" else None, timeout=15)
        if r.status_code != 401 or r.json().get("error", {}).get("code") != "UNAUTHORIZED":
            bad.append(f"{m} {p} -> {r.status_code}")
    check(not bad, f"{len(ROUTES)} routes refuse requests without a session" + (f": {bad}" if bad else ""))
    for m, p in PUBLIC:
        r = requests.request(m, url + "/api/v1" + p, json={} if m != "GET" else None, timeout=15)
        check(r.status_code != 401 or p == "/auth/session", f"public {m} {p} reachable ({r.status_code})")
    forged = requests.get(url + "/api/v1/lights", cookies={"lumen_session": "0" * 64}, timeout=10)
    check(forged.status_code == 401, "forged session cookie refused")

    S = Session(url, a.password)
    cookie = S.login_response.headers.get("Set-Cookie", "")
    check("HttpOnly" in cookie and "SameSite=Strict" in cookie and "Path=/" in cookie, f"session cookie flags: {cookie.split(';', 1)[1].strip() if ';' in cookie else cookie}")
    check(S.csrf not in cookie, "CSRF token is not in a cookie")

    # 2. CSRF on every mutating route (rejected before the handler runs).
    bad = []
    for m, p in ROUTES:
        if m == "GET":
            continue
        for hdr in ({}, {"X-CSRF-Token": "wrong" + S.csrf[5:]}):
            r = S.s.request(m, url + "/api/v1" + p, json={}, headers=hdr, timeout=15)
            if r.status_code != 403 or r.json().get("error", {}).get("code") != "CSRF_FAILED":
                bad.append(f"{m} {p} {'missing' if not hdr else 'wrong'} -> {r.status_code}")
    check(not bad, "every mutating route requires a valid CSRF token" + (f": {bad}" if bad else ""))

    # 3. Security headers on API and static responses.
    for path in ("/api/v1/lights", "/", "/recovery"):
        r = S.s.get(url + path, timeout=10)
        h = r.headers
        check(h.get("X-Content-Type-Options") == "nosniff" and h.get("X-Frame-Options") == "DENY" and "frame-ancestors 'none'" in h.get("Content-Security-Policy", ""),
              f"security headers on {path}")
    check("no-store" in S.s.get(url + "/api/v1/lights", timeout=10).headers.get("Cache-Control", ""), "API responses are not cached")

    # 4. No secrets in any GET response (except the explicit reveal routes).
    leaks = []
    for m, p in ROUTES:
        if m != "GET" or "/x" in p or p in REVEALS:
            continue
        body = S.call("GET", p, timeout=40 if p in SLOW_GET else 15).text
        for sec in secrets:
            if sec and sec in body:
                leaks.append(f"{p} contains a secret")
        if re.search(r'"(password|passphrase|psk|secret|token)"\s*:\s*"[^"]+', body, re.I) and p != "/auth/session":
            leaks.append(f"{p} has a non-empty secret-like field")
    tok = S.call("GET", "/integrations/token").json()["data"]["token"]
    for m, p in ROUTES:
        if m == "GET" and "/x" not in p and p not in REVEALS and tok in S.call("GET", p, timeout=40 if p in SLOW_GET else 15).text:
            leaks.append(f"{p} contains the integration token")
    check(not leaks, "no secrets in GET responses" + (f": {leaks}" if leaks else ""))
    diag = S.call("GET", "/system/diagnostics").text
    check(not any(s in diag for s in secrets + [tok]), "diagnostics contain no secrets")

    # 5. Errors: JSON envelope, no internals; size limits.
    r = S.call("GET", "/nope/nope")
    check(r.status_code == 404 and r.json()["success"] is False, "unknown route -> 404 envelope")
    r = S.s.post(url + "/api/v1/lights", data="{not json", headers={"X-CSRF-Token": S.csrf, "Content-Type": "application/json"}, timeout=10)
    check(r.status_code == 400 and r.json()["success"] is False, "malformed JSON -> 400 envelope")
    r = S.s.post(url + "/api/v1/lights", data=b"x" * (17 * 1024), headers={"X-CSRF-Token": S.csrf, "Content-Type": "application/json"}, timeout=20)
    check(r.status_code == 413, f"oversized body -> 413 ({r.status_code})")
    r = S.s.put(url + "/api/v1/integrations/mqtt", data=b"x" * 5000, headers={"X-CSRF-Token": S.csrf, "Content-Type": "application/json"}, timeout=20)
    check(r.status_code == 413, f"per-route limit (4 KB) -> 413 ({r.status_code})")
    r = S.call("POST", "/system/factory-reset", {})
    check(r.status_code == 400, "factory reset requires an explicit confirmation")

    # 6. WebSocket: no upgrade without a session; closed on logout.
    import websockets

    ws_url = url.replace("http", "ws") + "/api/v1/ws"

    async def ws_checks():
        try:
            async with websockets.connect(ws_url, open_timeout=5):
                return "opened-unauth"
        except Exception:  # noqa: BLE001 — any refusal is fine
            pass
        T = Session(url, a.password)
        cookie = f"lumen_session={T.s.cookies.get('lumen_session')}"
        async with websockets.connect(ws_url, extra_headers={"Cookie": cookie}, open_timeout=5) as ws:
            T.call("POST", "/auth/logout")
            try:
                await asyncio.wait_for(ws.wait_closed(), 5)
                return "closed"
            except asyncio.TimeoutError:
                return "still-open"

    res = asyncio.run(ws_checks())
    check(res == "closed", f"WebSocket refused without a session and closed on logout ({res})")

    # 7. Sessions end on logout and on password change.
    T = Session(url, a.password)
    T.call("POST", "/auth/logout")
    check(T.call("GET", "/lights").status_code == 401, "logout ends the session")
    A, B = Session(url, a.password), Session(url, a.password)
    temp = "Tmp-" + str(int(time.time())) + "-Pass!"
    r = A.call("PUT", "/auth/password", {"currentPassword": a.password, "newPassword": temp}, timeout=30)
    if check(r.ok, "password change accepted"):
        check(B.call("GET", "/lights").status_code == 401, "password change ends other sessions")
        try:
            C = Session(url, temp)
            r2 = C.call("PUT", "/auth/password", {"currentPassword": temp, "newPassword": a.password}, timeout=30)
            check(r2.ok, "password restored")
        except Exception as e:  # noqa: BLE001
            print(f"!!! password left at {temp!r}: {e}")
            raise
        r = A.call("PUT", "/auth/password", {"currentPassword": a.password, "newPassword": "short"}, timeout=30)
        check(r.status_code in (400, 401), "weak password refused")

    # 8. Login rate limit (last).
    if not a.skip_ratelimit:
        codes = []
        for _ in range(7):
            r = requests.post(url + "/api/v1/auth/login", json={"username": "admin", "password": "wrong-password"}, timeout=15)
            codes.append(r.status_code)
            if r.status_code == 429:
                check(int(r.headers.get("Retry-After", "0")) > 0, f"429 carries Retry-After ({r.headers.get('Retry-After')} s)")
                break
        check(429 in codes and codes.index(429) <= 6, f"login rate limit after repeated failures ({codes})")
        r = requests.post(url + "/api/v1/auth/login", json={"username": "admin", "password": a.password}, timeout=15)
        check(r.status_code == 429, "correct password is also held back while rate limited")
        print("      waiting for the rate-limit window to pass …", flush=True)
        t = time.time()
        while time.time() - t < 400:
            time.sleep(20)
            if requests.post(url + "/api/v1/auth/login", json={"username": "admin", "password": a.password}, timeout=15).ok:
                break
        check(time.time() - t < 400, f"sign-in works again after {time.time() - t:.0f} s")

    print(f"\n{sum(RESULTS)}/{len(RESULTS)} passed")
    sys.exit(0 if all(RESULTS) else 1)


if __name__ == "__main__":
    main()
