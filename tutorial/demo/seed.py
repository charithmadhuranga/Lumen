#!/usr/bin/env python3
"""Prepare both controllers for the project demo recording (idempotent).

    python3 tutorial/demo/seed.py            # reads tutorial/demo/.secrets.env

KC868-AP (LUMEN_KC):  rooms, 8 lights on PCA9685 channels 0-12 + both relays,
                      a group, the "Evening" scene, a sunset automation, a
                      floor plan with lights and room outlines, MQTT to the
                      Home Assistant test broker, ESP-NOW network "home".
                      PCA9685 channels 13-15 stay free for the Devices section.
DevKit   (LUMEN_DEV): generic ESP32 PWM profile, a studio room with two LEDC
                      lights, ESP-NOW network "home" (same key).

Secrets (admin password, broker password, ESP-NOW key) come from gitignored
files and are never printed.
"""

from __future__ import annotations

import os
import secrets
import socket
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(REPO / "test" / "device"))
from lumen_client import ApiError, Lumen  # noqa: E402


def env_file(p: Path) -> dict:
    out = {}
    if p.exists():
        for line in p.read_text().splitlines():
            if "=" in line and not line.lstrip().startswith("#"):
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    return out


ENV = {**env_file(HERE / ".secrets.env"), **os.environ}
HA_ENV = env_file(REPO / "test" / "integration" / "ha" / ".env")
PW = ENV["LUMEN_PASSWORD"]


def ok(fn, *a):
    """Create-if-missing: 409 (exists) is fine."""
    try:
        return fn(*a)
    except ApiError as e:
        if e.status != 409:
            raise
        return None


def upsert(L: Lumen, coll: str, body: dict):
    try:
        L.get(f"/{coll}/{body['id']}")
        return L.put(f"/{coll}/{body['id']}", body)
    except ApiError as e:
        if e.status != 404:
            raise
        return L.post(f"/{coll}", body)


def lan_ip_towards(host: str) -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((host, 80))
    ip = s.getsockname()[0]
    s.close()
    return ip


def espnow_key() -> str:
    f = HERE / ".espnow.key"
    if not f.exists():
        f.write_text(secrets.token_hex(16))
        f.chmod(0o600)
    return f.read_text().strip()


# ------------------------------------------------------------- KC868-AP
PCA = {"type": "pca9685", "deviceId": "pca9685_0"}
KC_ROOMS = [
    {"id": "living", "name": "Living Room", "floor": "Ground floor", "icon": "sofa"},
    {"id": "kitchen", "name": "Kitchen", "floor": "Ground floor", "icon": "utensils"},
    {"id": "bedroom", "name": "Bedroom", "floor": "First floor", "icon": "bed"},
    {"id": "outdoor", "name": "Outdoor", "floor": "Outside", "icon": "trees"},
]
KC_LIGHTS = [
    ("ceiling", "Ceiling", "dimmer", "living", {**PCA, "channel": 0}),
    ("tv_backlight", "TV backlight", "rgb", "living", {**PCA, "channel": 1}),
    ("floor_lamp", "Floor lamp", "cct", "living", {**PCA, "channel": 4}),
    ("counter", "Counter", "cct", "kitchen", {**PCA, "channel": 6}),
    ("pendants", "Pendants", "dimmer", "kitchen", {**PCA, "channel": 8}),
    ("bedside", "Bedside", "rgbw", "bedroom", {**PCA, "channel": 9}),
    ("porch", "Porch", "switch", "outdoor", {"type": "relay", "deviceId": "relay1", "channel": 0}),
    ("garden", "Garden", "switch", "outdoor", {"type": "relay", "deviceId": "relay2", "channel": 0}),
]
KC_STATE = {
    "ceiling": {"power": True, "brightness": 70},
    "tv_backlight": {"power": True, "brightness": 80, "red": 120, "green": 60, "blue": 255},
    "floor_lamp": {"power": True, "brightness": 45, "colorTemperature": 2700},
    "counter": {"power": True, "brightness": 85, "colorTemperature": 4500},
    "pendants": {"power": False},
    "bedside": {"power": False},
    "porch": {"power": True},
    "garden": {"power": False},
}

FLOORPLAN_SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 620">
  <rect x="0" y="0" width="1000" height="620" fill="#1b1f27"/>
  <g fill="#232833" stroke="#5d6675" stroke-width="8" stroke-linejoin="round">
    <rect x="40" y="40" width="520" height="330"/>
    <rect x="560" y="40" width="400" height="330"/>
    <rect x="40" y="370" width="460" height="210"/>
    <rect x="500" y="370" width="460" height="210"/>
  </g>
  <g fill="#1b1f27">
    <rect x="300" y="362" width="90" height="16"/>
    <rect x="552" y="150" width="16" height="90"/>
    <rect x="700" y="362" width="90" height="16"/>
  </g>
  <g fill="#2c3240" stroke="#3b4352" stroke-width="3">
    <rect x="90" y="250" width="230" height="80" rx="14"/>
    <rect x="380" y="90" width="140" height="34" rx="6"/>
    <rect x="600" y="70" width="330" height="60" rx="6"/>
    <rect x="700" y="210" width="150" height="90" rx="10"/>
    <rect x="80" y="420" width="190" height="130" rx="12"/>
    <rect x="560" y="430" width="380" height="110" rx="8"/>
  </g>
  <g font-family="sans-serif" font-size="26" fill="#8d96a6">
    <text x="60" y="80">Living room</text>
    <text x="580" y="170">Kitchen</text>
    <text x="300" y="560">Bedroom</text>
    <text x="520" y="410">Porch and garden</text>
  </g>
</svg>
"""
FLOORPLAN_LAYOUT = {
    "lights": [
        {"lightId": "ceiling", "x": 260, "y": 190, "radius": 150},
        {"lightId": "tv_backlight", "x": 450, "y": 108, "radius": 110},
        {"lightId": "floor_lamp", "x": 90, "y": 220, "radius": 90},
        {"lightId": "counter", "x": 765, "y": 100, "radius": 120},
        {"lightId": "pendants", "x": 775, "y": 255, "radius": 110},
        {"lightId": "bedside", "x": 300, "y": 460, "radius": 110},
        {"lightId": "porch", "x": 600, "y": 500, "radius": 90},
        {"lightId": "garden", "x": 860, "y": 500, "radius": 120},
    ],
    "rooms": [
        {"roomId": "living", "points": [[40, 40], [560, 40], [560, 370], [40, 370]]},
        {"roomId": "kitchen", "points": [[560, 40], [960, 40], [960, 370], [560, 370]]},
        {"roomId": "bedroom", "points": [[40, 370], [500, 370], [500, 580], [40, 580]]},
        {"roomId": "outdoor", "points": [[500, 370], [960, 370], [960, 580], [500, 580]]},
    ],
}


# Created on camera by the recording; removed so every take starts the same.
RECORDED = [("lights", "reading_lamp"), ("scenes", "movie_night"),
            ("automations", "porch_button")]


def reset_recorded(L: Lumen) -> None:
    for coll, rid in RECORDED:
        try:
            L.delete(f"/{coll}/{rid}")
            print(f"  removed {coll}/{rid}")
        except ApiError as e:
            if e.status != 404:
                raise


def seed_kc(L: Lumen) -> None:
    reset_recorded(L)
    for r in KC_ROOMS:
        ok(L.post, "/rooms", r)
    for lid, name, typ, room, drv in KC_LIGHTS:
        body = {"id": lid, "name": name, "type": typ, "roomId": room, "driver": drv,
                "transitionMs": 600}
        upsert(L, "lights", body)
    upsert(L, "groups", {"id": "downstairs", "name": "Downstairs", "roomId": "",
                         "channelIds": ["ceiling", "tv_backlight", "floor_lamp", "counter",
                                        "pendants"], "policy": "average"})
    for lid, st in KC_STATE.items():
        L.post(f"/lights/{lid}/command", {**st, "transitionMs": 0})
    time.sleep(1.0)
    # "Evening": warm and dim downstairs, porch on.
    evening = {"id": "evening", "name": "Evening", "transitionMs": 1500, "states": [
        {"lightId": "ceiling", "power": True, "brightness": 35},
        {"lightId": "floor_lamp", "power": True, "brightness": 60, "colorTemperature": 2700},
        {"lightId": "tv_backlight", "power": True, "brightness": 50, "red": 255, "green": 140,
         "blue": 40},
        {"lightId": "counter", "power": True, "brightness": 40, "colorTemperature": 3000},
        {"lightId": "porch", "power": True},
    ]}
    upsert(L, "scenes", evening)
    try:
        L.put("/automations/settings", {"latitude": 51.5, "longitude": -0.12})
    except ApiError as e:
        print("  automations/settings:", e)
    auto = {"id": "evening_lights", "name": "Evening lights at sunset", "enabled": True,
            "mode": "simple",
            "trigger": {"type": "sunset", "offset_min": -15},
            "actions": [{"type": "scene_activate", "target": "evening"},
                        {"type": "light_on", "target": "garden"}]}
    try:
        upsert(L, "automations", auto)
    except ApiError as e:
        print("  automation:", e, e.data)

    plans = L.get("/floorplans")
    if not any(p["id"] == "home" for p in plans):
        L.post("/floorplans", {"id": "home", "name": "Home"})
    up = L.s.post(f"{L.base}/api/v1/floorplans/home/upload", data=FLOORPLAN_SVG.encode(),
                  headers={"X-CSRF-Token": L.csrf, "Content-Type": "image/svg+xml"},
                  timeout=30)
    if not up.ok:
        raise SystemExit(f"floor plan upload failed: {up.status_code} {up.text[:200]}")
    L.put("/floorplans/home/layout", FLOORPLAN_LAYOUT)

    if HA_ENV.get("MQTT_LUMEN_PASSWORD"):
        broker = ENV.get("LUMEN_BROKER_HOST") or lan_ip_towards(L.base.split("//")[1])
        L.put("/integrations/mqtt", {"enabled": True, "host": broker, "port": 18830,
                                     "tls": False, "username": "lumen",
                                     "password": HA_ENV["MQTT_LUMEN_PASSWORD"],
                                     "topicPrefix": "lighting", "controllerId": "", "qos": 0,
                                     "retain": True})
        L.put("/integrations/homeassistant", {"enabled": True, "prefix": "homeassistant"})
        print(f"  MQTT -> {broker}:18830 (HA test broker)")
    else:
        print("  MQTT skipped: run test/integration/ha/setup.sh first")


# ---------------------------------------------------------------- DevKit
def seed_dev(L: Lumen) -> None:
    active = L.get("/boards/active")
    if (active or {}).get("id") != "generic_esp32_pwm":
        L.post("/boards/generic_esp32_pwm/apply", {})
        print("  applied profile generic_esp32_pwm")
        time.sleep(3)
    ok(L.post, "/rooms", {"id": "studio", "name": "Studio", "floor": "Garage", "icon": "lamp-desk"})
    led = lambda res: {"type": "ledc", "deviceId": res, "channel": 0}  # noqa: E731
    for lid, name, typ, drv in [("desk_lamp", "Desk lamp", "dimmer", led("ledc_pwm0")),
                                ("shelf_light", "Shelf light", "dimmer", led("ledc_pwm1"))]:
        try:
            upsert(L, "lights", {"id": lid, "name": name, "type": typ, "roomId": "studio",
                                 "driver": drv, "transitionMs": 600})
        except ApiError as e:
            print(f"  light {lid}: {e} {e.data}")
    for lid, st in {"desk_lamp": {"power": True, "brightness": 60},
                    "shelf_light": {"power": True, "brightness": 70}}.items():
        try:
            L.post(f"/lights/{lid}/command", {**st, "transitionMs": 0})
        except ApiError as e:
            print(f"  command {lid}: {e}")


def main() -> None:
    key = espnow_key()
    for name, url, fn in (("KC868-AP", ENV["LUMEN_KC"], seed_kc),
                          ("DevKit", ENV["LUMEN_DEV"], seed_dev)):
        print(f"== {name} {url}")
        L = Lumen(url, timeout=30)
        L.login("admin", PW)
        fn(L)
        L.put("/integrations/espnow", {"enabled": True, "networkId": "home", "key": key})
        print("  ESP-NOW network 'home' enabled")
    print("demo data ready")


if __name__ == "__main__":
    main()
