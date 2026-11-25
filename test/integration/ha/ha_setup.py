"""Unattended Home Assistant onboarding + MQTT integration for the test stack.

Reads .env (written by setup.sh). Writes the refresh token to .ha_token
(gitignored) so ha_test.py can call the HA API. Idempotent.
"""
import json
import os
import pathlib
import sys
import time

import requests

HERE = pathlib.Path(__file__).resolve().parent
HA = os.environ.get("HA_URL", "http://127.0.0.1:8124")
CLIENT_ID = HA + "/"
TOKEN_FILE = HERE / ".ha_token"


def env():
    out = {}
    for line in (HERE / ".env").read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def wait_for_ha(timeout=300):
    t = time.time()
    while time.time() - t < timeout:
        try:
            r = requests.get(HA + "/api/onboarding", timeout=3)
            if r.status_code in (200, 401, 404):
                return r
        except requests.RequestException:
            pass
        time.sleep(3)
    sys.exit("Home Assistant did not come up on " + HA)


def token_from_code(code):
    r = requests.post(HA + "/auth/token", data={"grant_type": "authorization_code", "code": code, "client_id": CLIENT_ID}, timeout=10)
    r.raise_for_status()
    return r.json()


def access_token():
    """Fresh access token from the stored refresh token."""
    refresh = TOKEN_FILE.read_text().strip()
    r = requests.post(HA + "/auth/token", data={"grant_type": "refresh_token", "refresh_token": refresh, "client_id": CLIENT_ID}, timeout=10)
    r.raise_for_status()
    return r.json()["access_token"]


def onboard(e):
    steps = wait_for_ha().json() if requests.get(HA + "/api/onboarding", timeout=5).status_code == 200 else []
    pending = {s["step"] for s in steps if not s.get("done")}
    if "user" in pending:
        r = requests.post(HA + "/api/onboarding/users", json={
            "client_id": CLIENT_ID, "name": "Lumen Test", "username": "lumen-test",
            "password": e["HA_OWNER_PASSWORD"], "language": "en"}, timeout=30)
        r.raise_for_status()
        tok = token_from_code(r.json()["auth_code"])
        TOKEN_FILE.write_text(tok["refresh_token"])
        TOKEN_FILE.chmod(0o600)
        print("onboarding: owner user created")
    elif not TOKEN_FILE.exists():
        # Already onboarded (e.g. token file deleted): log in with the owner.
        flow = requests.post(HA + "/auth/login_flow", json={"client_id": CLIENT_ID, "handler": ["homeassistant", None], "redirect_uri": CLIENT_ID}, timeout=10).json()
        r = requests.post(HA + f"/auth/login_flow/{flow['flow_id']}", json={"client_id": CLIENT_ID, "username": "lumen-test", "password": e["HA_OWNER_PASSWORD"]}, timeout=10).json()
        tok = token_from_code(r["result"])
        TOKEN_FILE.write_text(tok["refresh_token"])
        TOKEN_FILE.chmod(0o600)
    h = {"Authorization": "Bearer " + access_token()}
    for step, body in (("core_config", {}), ("analytics", {}), ("integration", {"client_id": CLIENT_ID, "redirect_uri": CLIENT_ID})):
        if step in pending:
            r = requests.post(HA + f"/api/onboarding/{step}", json=body, headers=h, timeout=30)
            print(f"onboarding: {step} -> {r.status_code}")
    return h


def ensure_mqtt(h, e):
    entries = requests.get(HA + "/api/config/config_entries/entry", headers=h, timeout=10).json()
    if any(x["domain"] == "mqtt" for x in entries):
        print("mqtt integration: already configured")
        return
    flow = requests.post(HA + "/api/config/config_entries/flow", json={"handler": "mqtt", "show_advanced_options": True}, headers=h, timeout=30).json()
    broker = {"broker": "mosquitto", "port": 1883, "username": "homeassistant", "password": e["MQTT_HA_PASSWORD"]}
    for _ in range(4):
        if flow.get("type") == "create_entry":
            print("mqtt integration: created")
            return
        if flow.get("type") == "menu":
            # Prefer manual broker entry when HA offers a menu.
            opt = next((o for o in flow["menu_options"] if "broker" in o or "manual" in o), flow["menu_options"][0])
            flow = requests.post(HA + f"/api/config/config_entries/flow/{flow['flow_id']}", json={"next_step_id": opt}, headers=h, timeout=30).json()
            continue
        if flow.get("type") == "form":
            schema = flow.get("data_schema", [])
            fields = {f["name"] for f in schema}
            body = {k: v for k, v in broker.items() if k in fields}
            # Newer HA groups advanced options in an expandable section whose
            # required keys must be present; send the plain-TCP defaults.
            for sec in (f for f in schema if f.get("type") == "expandable"):
                body[sec["name"]] = {"set_client_cert": False, "set_ca_cert": "off", "transport": "tcp"}
            flow = requests.post(HA + f"/api/config/config_entries/flow/{flow['flow_id']}", json=body, headers=h, timeout=30).json()
            if flow.get("errors"):
                sys.exit(f"mqtt flow error: {flow['errors']}")
            continue
        sys.exit(f"unexpected flow result: {json.dumps(flow)[:400]}")
    sys.exit("mqtt flow did not finish")


if __name__ == "__main__":
    e = env()
    wait_for_ha()
    h = onboard(e)
    ensure_mqtt(h, e)
    print("Home Assistant ready at", HA, "(user lumen-test, password in test/integration/ha/.env)")
