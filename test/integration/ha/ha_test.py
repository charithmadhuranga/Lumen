"""End-to-end integration tests: Lumen controller <-> Mosquitto <-> Home Assistant,
plus the local control interfaces (HTTP REST, TCP 8266, UDP 8267).

Runs against the real controller and the docker stack from setup.sh.
Every hardware assertion uses PCA9685/relay register read-back.

    python3 ha_test.py http://<controller-ip> '<admin password>' [--only mqtt,ha,http,tcp,udp] [--keep-going]

The PC's LAN address (how the controller reaches the broker) is detected
automatically; override with LUMEN_BROKER_HOST.
"""
import argparse
import asyncio
import json
import os
import pathlib
import socket
import subprocess
import sys
import time
import traceback

import requests

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "device"))
from lumen_client import ApiError, Lumen  # noqa: E402
from lumen_udp import frame, send as udp_send  # noqa: E402
import ha_setup  # noqa: E402

HA = ha_setup.HA
MQTT_PORT = 18830
CTRL_PORT_TCP = 8266
CTRL_PORT_UDP = 8267


# --------------------------------------------------------------------------- helpers
class Ctx:
    def __init__(self, url, password):
        self.url = url.rstrip("/")
        self.host = self.url.split("//", 1)[1].split(":")[0].split("/")[0]
        self.password = password
        self.L = Lumen(self.url)
        self.L.login("admin", password)
        self.env = ha_setup.env()
        self._ha_token = None
        self._ha_token_t = 0
        self.cid = None  # controller id (MQTT)

    # ---- Home Assistant
    def ha_headers(self):
        if time.time() - self._ha_token_t > 1200:
            self._ha_token = ha_setup.access_token()
            self._ha_token_t = time.time()
        return {"Authorization": "Bearer " + self._ha_token}

    def ha_get(self, path):
        r = requests.get(HA + path, headers=self.ha_headers(), timeout=10)
        return r.json() if r.status_code == 200 else None

    def ha_call(self, domain, service, data):
        r = requests.post(HA + f"/api/services/{domain}/{service}", headers=self.ha_headers(), json=data, timeout=15)
        assert r.status_code == 200, f"HA {domain}.{service} -> {r.status_code} {r.text[:200]}"

    def ha_state(self, entity_id):
        return self.ha_get(f"/api/states/{entity_id}")

    def ha_ws(self, *messages):
        """Run HA WebSocket commands; returns the result payloads in order."""
        import websockets

        async def run():
            async with websockets.connect(HA.replace("http", "ws") + "/api/websocket", max_size=8 * 1024 * 1024) as ws:
                await ws.recv()
                await ws.send(json.dumps({"type": "auth", "access_token": self.ha_headers()["Authorization"][7:]}))
                assert json.loads(await ws.recv())["type"] == "auth_ok"
                out = []
                for i, m in enumerate(messages, 1):
                    await ws.send(json.dumps({"id": i, **m}))
                    while True:
                        r = json.loads(await ws.recv())
                        if r.get("id") == i and r.get("type") == "result":
                            out.append(r.get("result") if r.get("success") else {"error": r.get("error")})
                            break
                return out

        return asyncio.run(run())

    def ha_entity_for(self, light_id):
        """light entity whose unique_id is <cid>_<light_id> (registry lookup)."""
        reg = self.ha_ws({"type": "config/entity_registry/list"})[0]
        uid = f"{self.cid}_{light_id}"
        for e in reg:
            if e.get("unique_id") == uid and e.get("platform") == "mqtt":
                return e
        return None

    # ---- Lumen
    def readback(self, output):
        return self.L.get(f"/hardware/outputs/{output}/readback")["percent"]

    def light(self, lid):
        return self.L.get(f"/lights/{lid}")

    # ---- MQTT via the broker container
    def mqtt_pub(self, topic, payload, retain=False):
        cmd = ["docker", "compose", "-f", str(HERE / "docker-compose.yml"), "exec", "-T", "mosquitto", "mosquitto_pub",
               "-u", "lumen", "-P", self.env["MQTT_LUMEN_PASSWORD"], "-t", topic, "-m", payload]
        if retain:
            cmd.append("-r")
        subprocess.run(cmd, check=True, capture_output=True)

    def mqtt_retained(self, topic, wait=1.5):
        out = subprocess.run(["docker", "compose", "-f", str(HERE / "docker-compose.yml"), "exec", "-T", "mosquitto", "mosquitto_sub",
                              "-u", "lumen", "-P", self.env["MQTT_LUMEN_PASSWORD"], "-v", "--retained-only", "-t", topic, "-W", str(int(wait))],
                             capture_output=True, text=True).stdout
        res = {}
        for line in out.splitlines():
            t, _, p = line.partition(" ")
            res[t] = p
        return res


def until(fn, timeout=6.0, interval=0.25, desc="condition"):
    t = time.time()
    last = None
    while time.time() - t < timeout:
        try:
            last = fn()
            if last:
                return last
        except (AssertionError, KeyError, TypeError, ApiError, requests.RequestException, OSError) as e:
            last = e
        time.sleep(interval)
    raise AssertionError(f"timeout waiting for {desc} (last: {str(last)[:300]})")


def lan_ip_towards(host):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((host, 9))
        return s.getsockname()[0]
    finally:
        s.close()


RESULTS = []


def test(group):
    def deco(fn):
        fn.group = group
        TESTS.append(fn)
        return fn
    return deco


TESTS = []

# --------------------------------------------------------------------------- fixtures
DEMO = [
    {"id": "ceiling", "name": "Ceiling", "type": "dimmer", "roomId": "living", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 0}},
    {"id": "tv_strip", "name": "TV strip", "type": "rgb", "roomId": "living", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 1}},
    {"id": "counter", "name": "Counter", "type": "cct", "roomId": "kitchen", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 4}},
    {"id": "porch", "name": "Porch", "type": "switch", "driver": {"type": "relay", "deviceId": "relay1", "channel": 0}},
]


def prepare(c: Ctx):
    have = {l["id"] for l in c.L.get("/lights")}
    for room in ({"id": "living", "name": "Living room"}, {"id": "kitchen", "name": "Kitchen"}):
        try:
            c.L.post("/rooms", room)
        except ApiError:
            pass
    for l in DEMO:
        if l["id"] not in have:
            c.L.post("/lights", l)
    for lid in ("it_tmp", "it_tmp2"):
        if lid in have:
            c.L.delete(f"/lights/{lid}")
    for path in ("/groups/it_group", "/scenes/it_scene"):
        try:
            c.L.delete(path)
        except ApiError:
            pass
    c.L.post("/groups", {"id": "it_group", "name": "IT group", "channelIds": ["ceiling", "counter"]})
    # it_scene = ceiling on at 100 %, instant.
    c.L.post("/lights/ceiling/command", {"power": True, "brightness": 100, "transitionMs": 0})
    time.sleep(0.3)
    c.L.post("/scenes/capture", {"id": "it_scene", "name": "IT scene", "lightIds": ["ceiling"], "transitionMs": 0})
    for lid in ("ceiling", "tv_strip", "counter", "porch"):
        c.L.post(f"/lights/{lid}/command", {"power": False, "transitionMs": 0})
    broker = os.environ.get("LUMEN_BROKER_HOST") or lan_ip_towards(c.host)
    r = c.L.put("/integrations/mqtt", {"enabled": True, "host": broker, "port": MQTT_PORT, "tls": False, "username": "lumen",
                                       "password": c.env["MQTT_LUMEN_PASSWORD"], "topicPrefix": "lighting", "controllerId": "", "qos": 0, "retain": True})
    c.L.put("/integrations/homeassistant", {"enabled": True, "prefix": "homeassistant"})
    c.cid = r["baseTopic"].split("/", 1)[1]
    until(lambda: c.L.get("/integrations")["mqtt"]["status"]["connected"], 15, desc="Lumen MQTT connected")


# --------------------------------------------------------------------------- MQTT
@test("mqtt")
def mqtt_status_online_retained(c):
    r = c.mqtt_retained(f"lighting/{c.cid}/status")
    assert r.get(f"lighting/{c.cid}/status") == "online", r


@test("mqtt")
def mqtt_retained_state_for_every_light(c):
    r = c.mqtt_retained(f"lighting/{c.cid}/light/+/state")
    for l in DEMO:
        t = f"lighting/{c.cid}/light/{l['id']}/state"
        assert t in r, f"missing {t}"
        json.loads(r[t])


@test("mqtt")
def mqtt_set_topic_drives_hardware(c):
    c.mqtt_pub(f"lighting/{c.cid}/light/ceiling/set", json.dumps({"state": "ON", "brightness": 100, "transition": 0}))
    until(lambda: c.readback("pca9685_0:0") == 100, desc="ch0 at 100%")
    st = until(lambda: (lambda s: s if json.loads(s).get("brightness") == 100 else None)(c.mqtt_retained(f"lighting/{c.cid}/light/ceiling/state")[f"lighting/{c.cid}/light/ceiling/state"]), desc="state topic 100")
    assert json.loads(st)["state"] == "ON"
    c.mqtt_pub(f"lighting/{c.cid}/light/ceiling/set", json.dumps({"state": "OFF", "transition": 0}))
    until(lambda: c.readback("pca9685_0:0") == 0, desc="ch0 off")


@test("mqtt")
def mqtt_invalid_payloads_rejected_without_effect(c):
    before = c.L.get("/integrations")["mqtt"]["status"]["rejected"]
    for bad in ["not json", "[]", '{"brightness":500}', '{"state":"MAYBE"}', '{"color":{"r":999,"g":0,"b":0}}', '{"effect":"explode"}', "{}", '{"transition":-1,"state":"ON"}']:
        c.mqtt_pub(f"lighting/{c.cid}/light/ceiling/set", bad)
    c.mqtt_pub(f"lighting/{c.cid}/light/no_such_light/set", '{"state":"ON"}')
    until(lambda: c.L.get("/integrations")["mqtt"]["status"]["rejected"] - before >= 9, desc="9 rejections")
    assert c.readback("pca9685_0:0") == 0
    assert c.L.get("/integrations")["mqtt"]["status"]["connected"]


@test("mqtt")
def mqtt_group_and_scene_topics(c):
    c.mqtt_pub(f"lighting/{c.cid}/group/it_group/set", json.dumps({"state": "ON", "brightness": 100, "transition": 0}))
    until(lambda: c.readback("pca9685_0:0") == 100, desc="group member ceiling on")
    until(lambda: c.light("counter")["state"]["power"], desc="group member counter on")
    c.mqtt_pub(f"lighting/{c.cid}/group/it_group/set", json.dumps({"state": "OFF", "transition": 0}))
    until(lambda: c.readback("pca9685_0:0") == 0, desc="group off")
    c.mqtt_pub(f"lighting/{c.cid}/scene/it_scene/activate", "1")
    until(lambda: c.readback("pca9685_0:0") == 100, desc="scene restores ceiling 100%")
    c.L.post("/lights/ceiling/command", {"power": False, "transitionMs": 0})


@test("mqtt")
def mqtt_burst_coalesces_to_final_state(c):
    for b in range(5, 101, 5):
        c.mqtt_pub(f"lighting/{c.cid}/light/ceiling/set", json.dumps({"state": "ON", "brightness": b, "transition": 0}))
    until(lambda: c.light("ceiling")["state"]["brightness"] == 100 and c.readback("pca9685_0:0") == 100, desc="final 100%")
    until(lambda: json.loads(c.mqtt_retained(f"lighting/{c.cid}/light/ceiling/state")[f"lighting/{c.cid}/light/ceiling/state"])["brightness"] == 100, desc="state 100")
    st = c.L.get("/integrations")["mqtt"]["status"]
    assert st["dropped"] == 0, st
    c.L.post("/lights/ceiling/command", {"power": False, "transitionMs": 0})


@test("mqtt")
def mqtt_diagnostics_published(c):
    r = c.mqtt_retained(f"homeassistant/sensor/{c.cid}/+/config")
    assert len(r) == 3, r


# --------------------------------------------------------------------------- Home Assistant
def ha_light(c, lid, timeout=20):
    e = until(lambda: c.ha_entity_for(lid), timeout, desc=f"HA entity for {lid}")
    return e["entity_id"]


@test("ha")
def ha_discovers_all_lights_with_correct_modes(c):
    want = {"ceiling": ["brightness"], "tv_strip": ["rgb"], "counter": ["color_temp"], "porch": ["onoff"]}
    for lid, modes in want.items():
        eid = ha_light(c, lid)
        s = until(lambda: (lambda x: x if x and x["state"] != "unavailable" else None)(c.ha_state(eid)), 15, desc=f"{eid} available")
        assert s["attributes"].get("supported_color_modes") == modes, (lid, s["attributes"])
        if lid != "porch":
            assert "rainbow" in s["attributes"].get("effect_list", []), s["attributes"]


@test("ha")
def ha_entity_ids_and_device(c):
    eid = ha_light(c, "ceiling")
    assert eid == f"light.{c.cid.replace('-', '_')}_ceiling", eid
    devs = c.ha_ws({"type": "config/device_registry/list"})[0]
    mine = [d for d in devs if any(i[1] == f"lumen_{c.cid}" or i[1] == f"lumen_{c.cid.replace('-', '_')}" for i in d.get("identifiers", []))]
    assert len(mine) == 1, [d.get("identifiers") for d in devs]
    d = mine[0]
    assert d.get("manufacturer") and d.get("sw_version"), d
    ents = [e for e in c.ha_ws({"type": "config/entity_registry/list"})[0] if e.get("device_id") == d["id"]]
    assert len(ents) >= 7, [e["entity_id"] for e in ents]  # 4 lights + 3 sensors


@test("ha")
def ha_dimmer_brightness_roundtrip(c):
    eid = ha_light(c, "ceiling")
    c.ha_call("light", "turn_on", {"entity_id": eid, "brightness_pct": 50, "transition": 0})
    until(lambda: c.light("ceiling")["state"]["brightness"] == 50, desc="Lumen brightness 50")
    until(lambda: 0 < c.readback("pca9685_0:0") < 100, desc="ch0 partially on")
    s = until(lambda: (lambda x: x if x["state"] == "on" else None)(c.ha_state(eid)), desc="HA on")
    assert abs(s["attributes"]["brightness"] - 128) <= 1, s["attributes"]
    c.ha_call("light", "turn_on", {"entity_id": eid, "brightness": 255, "transition": 0})
    until(lambda: c.readback("pca9685_0:0") == 100, desc="ch0 100%")
    c.ha_call("light", "turn_off", {"entity_id": eid, "transition": 0})
    until(lambda: c.readback("pca9685_0:0") == 0, desc="ch0 off")
    until(lambda: c.ha_state(eid)["state"] == "off", desc="HA off")


@test("ha")
def ha_rgb_color(c):
    eid = ha_light(c, "tv_strip")
    c.ha_call("light", "turn_on", {"entity_id": eid, "rgb_color": [255, 0, 0], "brightness": 255, "transition": 0})
    until(lambda: c.readback("pca9685_0:1") == 100 and c.readback("pca9685_0:2") == 0 and c.readback("pca9685_0:3") == 0, desc="red only")
    s = until(lambda: (lambda x: x if x["attributes"].get("rgb_color") == [255, 0, 0] else None)(c.ha_state(eid)), desc="HA red")
    assert s["attributes"]["color_mode"] == "rgb"
    c.ha_call("light", "turn_on", {"entity_id": eid, "rgb_color": [0, 0, 255], "transition": 0})
    until(lambda: c.readback("pca9685_0:3") == 100 and c.readback("pca9685_0:1") == 0, desc="blue only")
    c.ha_call("light", "turn_off", {"entity_id": eid, "transition": 0})
    until(lambda: max(c.readback(f"pca9685_0:{i}") for i in (1, 2, 3)) == 0, desc="strip off")


@test("ha")
def ha_color_temperature(c):
    eid = ha_light(c, "counter")
    c.ha_call("light", "turn_on", {"entity_id": eid, "color_temp_kelvin": 6500, "brightness": 255, "transition": 0})
    until(lambda: c.light("counter")["state"]["colorTemperature"] == 6500, desc="Lumen 6500 K")
    until(lambda: c.readback("pca9685_0:5") > c.readback("pca9685_0:4"), desc="cool channel dominant")
    c.ha_call("light", "turn_on", {"entity_id": eid, "color_temp_kelvin": 2700, "transition": 0})
    until(lambda: c.readback("pca9685_0:4") > c.readback("pca9685_0:5"), desc="warm channel dominant")
    s = until(lambda: (lambda x: x if x["attributes"].get("color_temp_kelvin") else None)(c.ha_state(eid)), desc="HA kelvin")
    assert abs(s["attributes"]["color_temp_kelvin"] - 2700) <= 30, s["attributes"]
    assert s["attributes"]["min_color_temp_kelvin"] == 2700 and s["attributes"]["max_color_temp_kelvin"] == 6500, s["attributes"]
    c.ha_call("light", "turn_off", {"entity_id": eid, "transition": 0})


@test("ha")
def ha_switch_relay(c):
    eid = ha_light(c, "porch")
    c.ha_call("light", "turn_on", {"entity_id": eid})
    until(lambda: c.readback("relay1") == 100, desc="relay on")
    # toggle is resolved by HA from its own state: wait until HA has seen "on".
    until(lambda: c.ha_state(eid)["state"] == "on", desc="HA on")
    c.ha_call("light", "toggle", {"entity_id": eid})
    until(lambda: c.readback("relay1") == 0, desc="relay off")
    until(lambda: c.ha_state(eid)["state"] == "off", desc="HA off")


@test("ha")
def ha_effect(c):
    eid = ha_light(c, "tv_strip")
    c.ha_call("light", "turn_on", {"entity_id": eid, "effect": "rainbow"})
    s = until(lambda: (lambda x: x if x["attributes"].get("effect") == "rainbow" else None)(c.ha_state(eid)), desc="HA effect rainbow")
    c.ha_call("light", "turn_on", {"entity_id": eid, "effect": "none"})
    until(lambda: c.ha_state(eid)["attributes"].get("effect") in (None, "none"), desc="effect cleared")
    c.ha_call("light", "turn_off", {"entity_id": eid, "transition": 0})


@test("ha")
def ha_transition(c):
    eid = ha_light(c, "ceiling")
    c.ha_call("light", "turn_on", {"entity_id": eid, "brightness": 255, "transition": 2})
    time.sleep(0.8)
    mid = c.readback("pca9685_0:0")
    assert 0 < mid < 100, f"expected mid-fade, got {mid}"
    until(lambda: c.readback("pca9685_0:0") == 100, 4, desc="fade complete")
    c.ha_call("light", "turn_off", {"entity_id": eid, "transition": 0})


@test("ha")
def ha_sees_changes_made_elsewhere(c):
    eid = ha_light(c, "ceiling")
    c.L.post("/lights/ceiling/command", {"power": True, "brightness": 25, "transitionMs": 0})
    until(lambda: c.ha_state(eid)["state"] == "on" and abs(c.ha_state(eid)["attributes"]["brightness"] - 64) <= 1, desc="HA follows REST change")
    c.L.post("/lights/ceiling/command", {"power": False, "transitionMs": 0})
    until(lambda: c.ha_state(eid)["state"] == "off", desc="HA off")


@test("ha")
def ha_diagnostic_sensors(c):
    reg = c.ha_ws({"type": "config/entity_registry/list"})[0]
    sens = [e for e in reg if e.get("platform") == "mqtt" and e["entity_id"].startswith("sensor.") and (e.get("unique_id") or "").startswith(c.cid)]
    assert len(sens) == 3, [e["entity_id"] for e in sens]
    for e in sens:
        assert e.get("entity_category") == "diagnostic", e
        s = until(lambda: (lambda x: x if x and x["state"] not in ("unknown", "unavailable") else None)(c.ha_state(e["entity_id"])), 70, desc=f"{e['entity_id']} value")
        float(s["state"])


@test("ha")
def ha_light_lifecycle(c):
    c.L.post("/lights", {"id": "it_tmp", "name": "IT Temp", "type": "dimmer", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 12}})
    eid = ha_light(c, "it_tmp")
    until(lambda: c.ha_state(eid)["state"] == "off", desc="new entity state")
    c.ha_call("light", "turn_on", {"entity_id": eid, "brightness": 255, "transition": 0})
    until(lambda: c.readback("pca9685_0:12") == 100, desc="new light works from HA")
    c.L.put("/lights/it_tmp", {"name": "IT Renamed"})
    until(lambda: "IT Renamed" in (c.ha_state(eid) or {}).get("attributes", {}).get("friendly_name", ""), 10, desc="rename reaches HA")
    c.L.delete("/lights/it_tmp")
    until(lambda: c.ha_entity_for("it_tmp") is None, 15, desc="entity removed from HA")
    assert c.readback("pca9685_0:12") == 0
    r = c.mqtt_retained(f"homeassistant/light/{c.cid}/it_tmp/config")
    r.update(c.mqtt_retained(f"lighting/{c.cid}/light/it_tmp/state"))
    assert not r, f"stale retained topics: {list(r)}"


@test("ha")
def ha_availability_follows_mqtt(c):
    eid = ha_light(c, "ceiling")
    c.L.put("/integrations/mqtt", {"enabled": False})
    until(lambda: c.ha_state(eid) is None or c.ha_state(eid)["state"] == "unavailable", 15, desc="unavailable/removed after disable")
    c.L.put("/integrations/mqtt", {"enabled": True})
    until(lambda: c.ha_state(eid) and c.ha_state(eid)["state"] == "off", 20, desc="available again")


@test("ha")
def ha_controller_restart(c):
    eid = ha_light(c, "ceiling")
    c.L.post("/lights/ceiling/command", {"power": True, "brightness": 40, "transitionMs": 0})
    until(lambda: c.ha_state(eid)["state"] == "on", desc="on before restart")
    c.L.post("/system/restart")
    until(lambda: c.ha_state(eid)["state"] == "unavailable", 10, desc="unavailable during restart")
    time.sleep(8)
    until(lambda: requests.get(c.url + "/api/v1/system/health", timeout=2).ok, 40, desc="controller back")
    until(lambda: c.L.login("admin", c.password) or True, 20, 1, desc="login after restart")
    until(lambda: c.ha_state(eid)["state"] == "on", 30, desc="available and on after restart")
    assert c.readback("pca9685_0:0") > 0
    c.ha_call("light", "turn_off", {"entity_id": eid, "transition": 0})
    until(lambda: c.readback("pca9685_0:0") == 0, desc="HA control works after restart")


@test("ha")
def ha_restart_rediscovery(c):
    """After an HA restart (birth message) entities and states are current."""
    eid = ha_light(c, "ceiling")
    subprocess.run(["docker", "compose", "-f", str(HERE / "docker-compose.yml"), "restart", "homeassistant"], check=True, capture_output=True)
    c._ha_token_t = 0
    until(lambda: requests.get(HA + "/api/", headers=c.ha_headers(), timeout=3).status_code == 200, 240, 3, desc="HA back")
    c.L.post("/lights/ceiling/command", {"power": True, "brightness": 30, "transitionMs": 0})
    until(lambda: c.ha_state(eid) and c.ha_state(eid)["state"] == "on", 60, 1, desc="HA tracks state after its restart")
    c.ha_call("light", "turn_off", {"entity_id": eid, "transition": 0})
    until(lambda: c.readback("pca9685_0:0") == 0, desc="HA control after its restart")


@test("ha")
def ha_broker_outage_and_recovery(c):
    """Lighting keeps working without the broker; on reconnect HA catches up."""
    eid = ha_light(c, "ceiling")
    compose = ["docker", "compose", "-f", str(HERE / "docker-compose.yml")]
    subprocess.run(compose + ["stop", "mosquitto"], check=True, capture_output=True)
    try:
        until(lambda: not c.L.get("/integrations")["mqtt"]["status"]["connected"], 20, desc="Lumen notices outage")
        c.L.post("/lights/ceiling/command", {"power": True, "brightness": 70, "transitionMs": 0})
        until(lambda: c.readback("pca9685_0:0") > 0, desc="local control during outage")
    finally:
        subprocess.run(compose + ["start", "mosquitto"], check=True, capture_output=True)
    until(lambda: c.L.get("/integrations")["mqtt"]["status"]["connected"], 30, desc="Lumen reconnects")
    until(lambda: c.ha_state(eid)["state"] == "on" and abs(c.ha_state(eid)["attributes"]["brightness"] - 178) <= 2, 30, desc="HA synced after outage")
    c.ha_call("light", "turn_off", {"entity_id": eid, "transition": 0})
    until(lambda: c.readback("pca9685_0:0") == 0, desc="HA control after outage")


# --------------------------------------------------------------------------- HTTP (REST local control)
@test("http")
def http_light_command_and_validation(c):
    c.L.post("/lights/ceiling/command", {"power": True, "brightness": 100, "transitionMs": 0})
    until(lambda: c.readback("pca9685_0:0") == 100, desc="REST on")
    for bad in ({"brightness": 101}, {"red": 300}, {}, {"transitionMs": -1}, {"effect": "explode"}):
        try:
            c.L.post("/lights/ceiling/command", bad)
            raise AssertionError(f"accepted {bad}")
        except ApiError as e:
            assert e.status in (400, 422), (bad, e.status)
    try:
        c.L.post("/lights/nope/command", {"power": True})
        raise AssertionError("unknown light accepted")
    except ApiError as e:
        assert e.status == 404
    c.L.post("/lights/ceiling/command", {"toggle": True, "transitionMs": 0})
    until(lambda: c.readback("pca9685_0:0") == 0, desc="toggle off")


@test("http")
def http_rooms_groups_scenes(c):
    c.L.post("/rooms/living/power", {"power": True})
    until(lambda: c.light("ceiling")["state"]["power"] and c.light("tv_strip")["state"]["power"], desc="room on")
    c.L.post("/rooms/living/power", {"power": False})
    until(lambda: not c.light("ceiling")["state"]["power"], desc="room off")
    c.L.post("/groups/it_group/brightness", {"brightness": 60})
    c.L.post("/groups/it_group/power", {"power": True})
    until(lambda: c.light("counter")["state"]["power"] and c.readback("pca9685_0:0") > 0, desc="group on")
    c.L.post("/groups/it_group/power", {"power": False})
    until(lambda: c.readback("pca9685_0:0") == 0, desc="group off")


@test("http")
def http_auth_csrf_enforced(c):
    r = requests.post(c.url + "/api/v1/lights/ceiling/command", json={"power": True}, timeout=5)
    assert r.status_code == 401, r.status_code
    s = requests.Session()
    lr = s.post(c.url + "/api/v1/auth/login", json={"username": "admin", "password": c.password}, timeout=10)
    assert lr.ok
    r = s.post(c.url + "/api/v1/lights/ceiling/command", json={"power": True}, timeout=5)
    assert r.status_code == 403 and r.json()["error"]["code"] == "CSRF_FAILED", r.text
    r = s.post(c.url + "/api/v1/lights/ceiling/command", json={"power": True}, headers={"X-CSRF-Token": "wrong"}, timeout=5)
    assert r.status_code == 403, r.status_code
    assert c.readback("pca9685_0:0") == 0
    r = requests.get(c.url + "/api/v1/integrations/token", timeout=5)
    assert r.status_code == 401
    s.post(c.url + "/api/v1/auth/logout", headers={"X-CSRF-Token": lr.json()["data"]["csrfToken"]}, timeout=5)


@test("http")
def http_secrets_never_returned(c):
    body = json.dumps(c.L.get("/integrations")) + json.dumps(c.L.get("/network"))
    assert c.env["MQTT_LUMEN_PASSWORD"] not in body
    assert '"password"' not in body.lower().replace('"haspassword"', "")


# --------------------------------------------------------------------------- TCP
class Tcp:
    def __init__(self, host, port=CTRL_PORT_TCP):
        self.s = socket.create_connection((host, port), timeout=5)
        self.f = self.s.makefile("rw")
        self.pushes = []

    def call(self, obj):
        self.f.write(json.dumps(obj) + "\n")
        self.f.flush()
        while True:
            line = self.f.readline()
            if not line:
                return None
            o = json.loads(line)
            if o.get("type") == "state":
                self.pushes.append(o)
                continue
            return o

    def close(self):
        # Both the file wrapper and the socket must be closed before Python
        # actually sends the FIN.
        for x in (self.f, self.s):
            try:
                x.close()
            except OSError:
                pass


def tcp_enabled(c):
    c.L.put("/integrations/tcp", {"tcpEnabled": True})
    time.sleep(1.2)
    return c.L.get("/integrations/token")["token"]


@test("tcp")
def tcp_full_command_set(c):
    tok = tcp_enabled(c)
    t = Tcp(c.host)
    try:
        assert t.call({"v": 1, "type": "auth", "token": tok})["ok"]
        assert t.call({"v": 1, "id": 1, "type": "light", "light_id": "ceiling", "state": "ON", "brightness": 100, "transition": 0}) == {"v": 1, "id": 1, "ok": True}
        until(lambda: c.readback("pca9685_0:0") == 100, desc="tcp light")
        g = t.call({"v": 1, "id": 2, "type": "get"})
        assert g["ok"] and {x["light_id"] for x in g["lights"]} >= {"ceiling", "tv_strip", "counter", "porch"}, g
        assert t.call({"v": 1, "id": 3, "type": "group", "group_id": "it_group", "state": "OFF", "transition": 0})["ok"]
        until(lambda: c.readback("pca9685_0:0") == 0, desc="tcp group off")
        assert t.call({"v": 1, "id": 4, "type": "light", "light_id": "tv_strip", "state": "ON", "color": {"r": 0, "g": 255, "b": 0}, "transition": 0})["ok"]
        until(lambda: c.readback("pca9685_0:2") == 100 and c.readback("pca9685_0:1") == 0, desc="tcp green")
        assert t.call({"v": 1, "id": 5, "type": "light", "light_id": "tv_strip", "state": "OFF", "transition": 0})["ok"]
        assert t.call({"v": 1, "id": 6, "type": "scene", "scene_id": "it_scene"})["ok"]
        assert t.call({"v": 1, "id": 7, "type": "ping"})["type"] == "pong"
        errs = [t.call(m) for m in (
            {"v": 1, "id": 8, "type": "light", "light_id": "zzz", "state": "ON"},
            {"v": 1, "id": 9, "type": "light", "light_id": "ceiling", "brightness": 101},
            {"v": 1, "id": 10, "type": "group", "group_id": "zzz", "state": "ON"},
            {"v": 1, "id": 11, "type": "scene", "scene_id": "zzz"},
            {"v": 1, "id": 12, "type": "dance"},
            {"v": 2, "id": 13, "type": "ping"},
        )]
        assert all(e and e["ok"] is False and e.get("error") for e in errs), errs
        assert t.call({"v": 1, "id": 14, "type": "ping"})["ok"], "connection survives errors"
    finally:
        t.close()
        c.L.post("/lights/ceiling/command", {"power": False, "transitionMs": 0})


@test("tcp")
def tcp_state_push_from_other_sources(c):
    tok = tcp_enabled(c)
    t = Tcp(c.host)
    try:
        t.call({"v": 1, "type": "auth", "token": tok})
        c.L.post("/lights/ceiling/command", {"power": True, "brightness": 33, "transitionMs": 0})
        t.call({"v": 1, "id": 1, "type": "ping"})
        until(lambda: (t.call({"v": 1, "id": 2, "type": "ping"}) and any(p["light_id"] == "ceiling" and p.get("brightness") == 33 for p in t.pushes)), desc="push of REST change")
    finally:
        t.close()
        c.L.post("/lights/ceiling/command", {"power": False, "transitionMs": 0})


@test("tcp")
def tcp_auth_and_limits(c):
    tok = tcp_enabled(c)
    t = Tcp(c.host)
    r = t.call({"v": 1, "id": 1, "type": "light", "light_id": "ceiling", "state": "ON"})
    assert r and r["ok"] is False and t.call({"v": 1, "type": "ping"}) is None, "unauthenticated must be closed"
    t.close()
    t = Tcp(c.host)
    assert t.call({"v": 1, "type": "auth", "token": tok[:-1] + ("0" if tok[-1] != "0" else "1")})["ok"] is False
    t.close()
    assert c.readback("pca9685_0:0") == 0
    # Oversized line drops the client.
    t = Tcp(c.host)
    t.call({"v": 1, "type": "auth", "token": tok})
    t.s.sendall(b"{" + b"x" * 2000 + b"}\n")
    t.s.settimeout(3)
    try:
        assert t.s.recv(100) in (b"",), "oversized line must close"
    except ConnectionResetError:
        pass
    t.close()
    # Client limit: 4 authenticated sessions, the 5th is refused.
    clients = [Tcp(c.host) for _ in range(4)]
    for x in clients:
        assert x.call({"v": 1, "type": "auth", "token": tok})["ok"]
    extra = Tcp(c.host)
    line = extra.f.readline()
    assert "too many" in line, line
    extra.close()
    for x in clients:
        x.close()
    # Slots are released.
    time.sleep(0.5)
    t = Tcp(c.host)
    assert t.call({"v": 1, "type": "auth", "token": tok})["ok"]
    t.close()


@test("tcp")
def tcp_auth_timeout(c):
    tcp_enabled(c)
    t = Tcp(c.host)
    t.s.settimeout(14)
    start = time.time()
    try:
        data = t.s.recv(100)
    except ConnectionResetError:
        data = b""
    took = time.time() - start
    assert data == b"" and 9 <= took <= 13, (data, took)
    t.close()


@test("tcp")
def tcp_disabled_refuses(c):
    c.L.put("/integrations/tcp", {"tcpEnabled": False})
    time.sleep(1.2)
    try:
        socket.create_connection((c.host, CTRL_PORT_TCP), timeout=3).close()
        raise AssertionError("port still open after disable")
    except (ConnectionRefusedError, socket.timeout, OSError) as e:
        if isinstance(e, AssertionError):
            raise


# --------------------------------------------------------------------------- UDP
@test("udp")
def udp_discovery_unicast_and_broadcast(c):
    c.L.put("/integrations/udp", {"udpEnabled": True})
    time.sleep(1)
    for msg in (b"LUMEN?", b'{"v":1,"type":"discover"}'):
        r = udp_send(c.host, msg)
        assert r and r["type"] == "announce" and r["ip"] == c.host, r
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.settimeout(3)
    s.sendto(b"LUMEN?", ("255.255.255.255", CTRL_PORT_UDP))
    found = []
    try:
        while True:
            found.append(json.loads(s.recv(512)))
    except socket.timeout:
        pass
    assert any(f.get("ip") == c.host for f in found), found
    assert udp_send(c.host, b"garbage", timeout=1) is None


@test("udp")
def udp_signed_control(c):
    tok = c.L.get("/integrations/token")["token"]
    on = {"type": "light", "light_id": "ceiling", "state": "ON", "brightness": 100, "transition": 0}
    c.L.put("/integrations/udp", {"udpEnabled": True, "udpControl": False})
    time.sleep(0.5)
    assert udp_send(c.host, frame(tok, on, 1), timeout=1) is None, "control must be off by default"
    c.L.put("/integrations/udp", {"udpControl": True})
    time.sleep(0.5)
    seq = 1
    src = f"it{int(time.time()) % 100000}"  # fresh sender: replay state is per sender

    def sign(tok_, cmd, seq_, ts=None):
        return frame(tok_, cmd, seq_, src=src, ts=ts)

    try:
        assert udp_send(c.host, sign(tok, on, seq))["ok"]
        until(lambda: c.readback("pca9685_0:0") == 100, desc="udp on")
        assert udp_send(c.host, sign(tok, {"type": "group", "group_id": "it_group", "state": "OFF", "transition": 0}, seq + 1))["ok"]
        until(lambda: c.readback("pca9685_0:0") == 0, desc="udp group off")
        assert udp_send(c.host, sign(tok, {"type": "scene", "scene_id": "it_scene"}, seq + 2))["ok"]
        until(lambda: c.readback("pca9685_0:0") == 100, desc="udp scene")
        r = udp_send(c.host, sign(tok, {"type": "light", "light_id": "zzz", "state": "ON"}, seq + 3))
        assert r and r["ok"] is False
        off = {"type": "light", "light_id": "ceiling", "state": "OFF", "transition": 0}
        assert udp_send(c.host, sign(tok, off, seq + 4))["ok"]
        until(lambda: c.readback("pca9685_0:0") == 0, desc="udp off")
        # All of these must be dropped and change nothing.
        for f in (sign(tok, on, seq + 4), sign(tok, on, seq), sign("wrong-token", on, seq + 10),
                  sign(tok, on, seq + 11, ts=int(time.time()) - 120), sign(tok, on, seq + 12, ts=int(time.time()) + 120),
                  json.dumps({"v": 1, "ts": int(time.time()), "seq": seq + 13, "src": "pc", "cmd": on}).encode()):
            assert udp_send(c.host, f, timeout=0.8) is None
        # Tampered command with a valid MAC for a different command.
        good = json.loads(sign(tok, off, seq + 14))
        good["cmd"] = on
        assert udp_send(c.host, json.dumps(good).encode(), timeout=0.8) is None
        assert c.readback("pca9685_0:0") == 0
        # Rate limit: >20 frames/s are dropped.
        sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sk.settimeout(0.05)
        base = seq + 100
        # ~100 frames/s for 0.5 s: paced so lwIP's small UDP mailbox does not
        # drop them first; the controller must answer at most 20 in a 1 s window.
        answered = 0
        sk.setblocking(False)
        t0 = time.time()
        for i in range(50):
            sk.sendto(sign(tok, {"type": "ping"}, base + i), (c.host, CTRL_PORT_UDP))
            time.sleep(0.01)
            try:
                while True:
                    sk.recv(512)
                    answered += 1
            except BlockingIOError:
                pass
        sent_in = time.time() - t0
        sk.settimeout(0.5)
        try:
            while True:
                sk.recv(512)
                answered += 1
        except socket.timeout:
            pass
        assert sent_in < 1.0, f"sender too slow ({sent_in:.2f}s)"
        assert 15 <= answered <= 21, f"answered {answered} of 50 (limit 20/s)"
    finally:
        c.L.put("/integrations/udp", {"udpControl": False})


@test("udp")
def udp_state_announce(c):
    c.L.put("/integrations/udp", {"udpEnabled": True, "udpAnnounce": True})
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", CTRL_PORT_UDP))
    s.settimeout(3)
    try:
        time.sleep(0.5)
        c.L.post("/lights/ceiling/command", {"power": True, "brightness": 20, "transitionMs": 0})
        got = None
        end = time.time() + 4
        while time.time() < end and not got:
            try:
                o = json.loads(s.recv(1024))
                if o.get("type") == "state" and o.get("light_id") == "ceiling":
                    got = o
            except (socket.timeout, ValueError):
                pass
        assert got and got["brightness"] == 20, got
    finally:
        s.close()
        c.L.put("/integrations/udp", {"udpAnnounce": False})
        c.L.post("/lights/ceiling/command", {"power": False, "transitionMs": 0})


# --------------------------------------------------------------------------- runner
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("password")
    ap.add_argument("--only", default="")
    ap.add_argument("-k", "--filter", default="")
    a = ap.parse_args()
    c = Ctx(a.url, a.password)
    prepare(c)
    groups = set(filter(None, a.only.split(",")))
    for t in TESTS:
        if groups and t.group not in groups:
            continue
        if a.filter and a.filter not in t.__name__:
            continue
        t0 = time.time()
        try:
            t(c)
            RESULTS.append((t.group, t.__name__, "PASS", f"{time.time() - t0:.1f}s"))
            print(f"PASS  [{t.group}] {t.__name__} ({time.time() - t0:.1f}s)", flush=True)
        except Exception as e:  # noqa: BLE001 — report and continue
            RESULTS.append((t.group, t.__name__, "FAIL", str(e)))
            print(f"FAIL  [{t.group}] {t.__name__}: {e}", flush=True)
            if not isinstance(e, AssertionError):
                traceback.print_exc()
            else:
                tb = traceback.extract_tb(e.__traceback__)[-1]
                print(f"      at line {tb.lineno}: {tb.line}")
            try:
                c.L.login("admin", c.password)
            except Exception:  # noqa: BLE001
                pass
    fails = [r for r in RESULTS if r[2] == "FAIL"]
    print(f"\n{len(RESULTS) - len(fails)}/{len(RESULTS)} passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
