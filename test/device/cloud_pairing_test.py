#!/usr/bin/env python3
"""Lumen Pro P5 on a real controller: gateway pairing, local tickets, scopes,
revocation, WebSocket, discovery hints, persistence over a restart.

    cd cloud && uv run python ../test/device/cloud_pairing_test.py http://<controller> '<admin password>' <this PC's LAN IP>

A fake gateway on this PC answers the controller's POST /local/v1/register
(the real Go gateway arrives in P6). Tickets are minted with the cloud's own
implementation (cloud/app/services/tickets.py), which passes the protocol
vectors. The controller is left UNPAIRED at the end.
"""

from __future__ import annotations

import base64
import json
import os
import secrets
import socket
import sys
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import requests

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "cloud"))

from lumen_client import ApiError, Lumen  # noqa: E402

from app.api.schemas import ControllerRegister  # noqa: E402
from app.services import tickets  # noqa: E402

BASE, PASSWORD, PC_IP = sys.argv[1].rstrip("/"), sys.argv[2], sys.argv[3]
GW_PORT = 8790
results: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    results.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""))


def uuid7() -> str:
    ms = int(time.time() * 1000)
    r = int.from_bytes(os.urandom(10), "big")
    v = (ms << 80) | (0x7 << 76) | (((r >> 62) & 0xFFF) << 64) | (0b10 << 62) | (r & ((1 << 62) - 1))
    return str(uuid.UUID(int=v))


DEVICE, GATEWAY = uuid7(), uuid7()
KEY = secrets.token_bytes(32)
received: list[dict] = []


class FakeGateway(BaseHTTPRequestHandler):
    def do_POST(self) -> None:  # noqa: N802
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        try:
            req = json.loads(body)
            ControllerRegister.model_validate(req)  # the cloud's protocol model (contract-tested)
            received.append(req)
            data = {"v": 1, "deviceId": DEVICE, "localKey": base64.urlsafe_b64encode(KEY).rstrip(b"=").decode(),
                    "ticketEpoch": 0, "gatewayId": GATEWAY}
            out, status = {"success": True, "data": data, "error": None}, 201
        except Exception as exc:  # noqa: BLE001
            out, status = {"success": False, "data": None, "error": {"code": "INVALID_REQUEST", "message": str(exc)[:100]}}, 400
        payload = json.dumps(out).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args: object) -> None:
        pass


def ticket(sub: str, scope: str = "control", iat: int | None = None, life: int | None = None, ep: int = 0,
           dev: str | None = None, key: bytes | None = None) -> str:
    now = int(time.time())
    iat = now - 60 if iat is None else iat
    life = life or (3600 if scope == "control" else 1800)
    claims = tickets.Claims(sub=sub, dev=dev or DEVICE, scp=scope, iat=iat, exp=iat + life, ep=ep)
    return tickets.mint(key or KEY, claims)


def call(method: str, path: str, t: str | None = None, body: object = None) -> requests.Response:
    headers = {"Content-Type": "application/json"}
    if t is not None:
        headers["Authorization"] = f"Lumen-Ticket {t}"
    return requests.request(method, f"{BASE}/api/v1{path}", headers=headers,
                            data=None if body is None else json.dumps(body), timeout=10)


def code(r: requests.Response) -> str:
    try:
        return (r.json().get("error") or {}).get("code") or ""
    except ValueError:
        return ""


def udp_announce() -> dict:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(2)
    host = BASE.split("//", 1)[1].split(":")[0]
    s.sendto(b"LUMEN?", (host, 8267))
    data, _ = s.recvfrom(1024)
    s.close()
    return json.loads(data)


def wait_state(c: Lumen, state: str, within: float = 15.0) -> dict:
    deadline = time.monotonic() + within
    while True:
        st = c.get("/cloud")
        if st["state"] == state or time.monotonic() > deadline:
            return st
        time.sleep(0.3)


def main() -> None:
    server = ThreadingHTTPServer(("0.0.0.0", GW_PORT), FakeGateway)  # noqa: S104 - LAN test server
    threading.Thread(target=server.serve_forever, daemon=True).start()
    c = Lumen(BASE)
    c.login("admin", PASSWORD)
    c.post("/cloud/unpair")
    heap_before = c.get("/system/diagnostics")["heap"]

    u = f"u:{uuid7()}"
    r = call("GET", "/lights", ticket(u))
    check("unpaired controller refuses tickets", r.status_code == 401 and code(r) == "INVALID_TICKET", r.text[:80])
    check("announce says cloud off", udp_announce().get("cloud") == "off")

    for bad in ({"claimToken": "nope", "gatewayUrl": f"http://{PC_IP}:{GW_PORT}"},
                {"claimToken": "lct_" + "A" * 43, "gatewayUrl": "http://8.8.8.8:8780"},
                {"claimToken": "lct_" + "A" * 43, "gatewayUrl": f"https://{PC_IP}"}):
        try:
            c.post("/cloud/pair", bad)
            check(f"pair refuses {bad}", False)
        except ApiError as e:
            check(f"pair refuses bad input ({bad['gatewayUrl'][:22]})", e.status == 400, e.message or "")

    claim = "lct_" + base64.urlsafe_b64encode(secrets.token_bytes(32)).rstrip(b"=").decode()
    st = c.post("/cloud/pair", {"claimToken": claim, "gatewayUrl": f"http://{PC_IP}:{GW_PORT}"})
    check("pair accepted (202, pairing)", st["state"] in ("pairing", "paired"))
    st = wait_state(c, "paired")
    check("paired within 15 s", st["state"] == "paired", json.dumps(st)[:120])
    req = received[-1] if received else {}
    check("register request matches the protocol", req.get("claimToken") == claim and req.get("v") == 1,
          f"controllerId={req.get('controllerId')} url={req.get('url')} hw={req.get('hw')}")
    check("status has device and gateway ids", st.get("deviceId") == DEVICE and st.get("gatewayId") == GATEWAY)
    ann = udp_announce()
    check("announce says paired + device id", ann.get("cloud") == "paired" and ann.get("device") == DEVICE)

    # --- scopes ---------------------------------------------------------------------------
    ctl = ticket(u)
    r = call("GET", "/lights", ctl)
    check("control ticket reads lights", r.status_code == 200)
    lights = r.json()["data"]
    light = lights[0]["id"] if lights else None
    if light:
        r = call("POST", f"/lights/{light}/command", ctl, {"toggle": True})
        check("control ticket sends a light command (no CSRF needed)", r.status_code == 202, r.text[:80])
        call("POST", f"/lights/{light}/command", ctl, {"toggle": True})  # back as it was
    for method, path, expect in (("GET", "/system/diagnostics", "FORBIDDEN"), ("GET", "/cloud", "FORBIDDEN"),
                                 ("POST", "/system/factory-reset", "SESSION_REQUIRED"),
                                 ("GET", "/integrations/token", "SESSION_REQUIRED"),
                                 ("PUT", "/auth/password", "SESSION_REQUIRED"),
                                 ("GET", "/network/ap-password", "SESSION_REQUIRED")):
        r = call(method, path, ctl, {} if method != "GET" else None)
        check(f"control ticket refused: {method} {path}", r.status_code == 403 and code(r) == expect, f"{r.status_code} {code(r)}")
    adm = ticket(f"g:{GATEWAY}", "admin")
    r = call("GET", "/system/diagnostics", adm)
    check("gateway admin ticket reads diagnostics", r.status_code == 200)
    r = call("GET", "/cloud", adm)
    seen = r.json()["data"].get("gatewaySeenSecAgo") if r.status_code == 200 else None
    check("gateway contact recorded", seen is not None and seen < 10, f"gatewaySeenSecAgo={seen}")
    for method, path in (("POST", "/cloud/pair"), ("POST", "/cloud/unpair"), ("POST", "/system/factory-reset")):
        r = call(method, path, adm, {})
        check(f"admin ticket refused: {method} {path}", r.status_code == 403 and code(r) == "SESSION_REQUIRED", code(r))

    # --- invalid tickets: the protocol reason reaches the client --------------------------------
    now = int(time.time())
    flipped = ctl[:-2] + ("A" if ctl[-2] != "A" else "B") + ctl[-1]
    cases = {
        "mac": flipped,
        "expired": ticket(u, iat=now - 7200, life=3600),
        "not_yet_valid": ticket(u, iat=now + 3600),
        "device": ticket(u, dev=uuid7()),
        "epoch": ticket(u, ep=5),
        "format": "LT1.garbage",
    }
    cases["mac"] = ticket(u, key=secrets.token_bytes(32))
    for reason, t in cases.items():
        r = call("GET", "/lights", t)
        check(f"ticket rejected: {reason}", r.status_code == 401 and reason in r.json()["error"]["message"], r.text[:90])
    r = requests.get(f"{BASE}/api/v1/lights", headers={"Authorization": "Lumen-Ticket " + "A" * 700}, timeout=10)
    check("oversized ticket rejected", r.status_code == 401, str(r.status_code))
    r = call("GET", "/lights", flipped)
    check("tampered ticket rejected", r.status_code == 401)

    # A session still needs CSRF; a ticket never fills in for a missing cookie on session routes.
    r = c.s.post(f"{BASE}/api/v1/system/restart", timeout=10)  # session cookie, no X-CSRF-Token
    check("session POST without CSRF still refused", r.status_code == 403 and code(r) == "CSRF_FAILED", code(r))

    # --- WebSocket with a ticket ---------------------------------------------------------------
    import websockets.sync.client as wsc  # cloud venv dependency

    host = BASE.split("//", 1)[1]
    try:
        wsc.connect(f"ws://{host}/api/v1/ws?ticket=LT1.bad.bad", open_timeout=5).close()
        check("WebSocket refuses a bad ticket", False)
    except Exception:  # noqa: BLE001
        check("WebSocket refuses a bad ticket", True)
    try:
        with wsc.connect(f"ws://{host}/api/v1/ws?ticket={ctl}", open_timeout=5) as ws:
            if light:
                call("POST", f"/lights/{light}/command", ctl, {"toggle": True})
                got = False
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and not got:
                    msg = json.loads(ws.recv(timeout=5))
                    got = msg.get("event") == "light_state_changed"
                call("POST", f"/lights/{light}/command", ctl, {"toggle": True})
                check("WebSocket with a control ticket receives light events", got)
    except Exception as exc:  # noqa: BLE001
        check("WebSocket with a control ticket receives light events", False, repr(exc)[:100])

    # --- revocation by epoch ---------------------------------------------------------------------
    r = call("PUT", "/cloud/epoch", adm, {"ticketEpoch": 1})
    check("gateway raises the ticket epoch", r.status_code == 200, r.text[:80])
    r = call("GET", "/lights", ctl)
    check("older tickets revoked by the epoch", r.status_code == 401 and "epoch" in r.text)
    r = call("GET", "/lights", ticket(u, ep=1))
    check("new-epoch ticket accepted", r.status_code == 200)
    r = call("PUT", "/cloud/epoch", ticket(f"g:{GATEWAY}", "admin", ep=1), {"ticketEpoch": 1})
    check("epoch cannot stay or go down", r.status_code == 409)

    # --- persistence over a restart ---------------------------------------------------------------
    c.post("/system/restart")
    time.sleep(3)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        try:
            if requests.get(f"{BASE}/api/v1/system/health", timeout=2).status_code == 200:
                break
        except requests.RequestException:
            pass
        time.sleep(1)
    r = call("GET", "/lights", ticket(u, ep=1))
    check("pairing and epoch survive a restart", r.status_code == 200, r.text[:80])
    c = Lumen(BASE)
    c.login("admin", PASSWORD)
    heap_after = c.get("/system/diagnostics")["heap"]

    # --- unpair -------------------------------------------------------------------------------
    st = c.post("/cloud/unpair")
    check("unpair", st["state"] == "off")
    r = call("GET", "/lights", ticket(u, ep=1))
    check("tickets refused after unpair", r.status_code == 401)
    check("announce says off again", udp_announce().get("cloud") == "off")

    print(f"\nheap before: {heap_before}\nheap after restart (paired): {heap_after}")
    failed = [n for n, ok, _ in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    server.shutdown()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
