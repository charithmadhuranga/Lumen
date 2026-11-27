#!/usr/bin/env python3
"""Generate protocol/fixtures from the examples below.

    python3 protocol/tools/make_fixtures.py

Writes protocol/fixtures/<schema>/valid/*.json, .../invalid/<rule>.json and
protocol/fixtures/tickets.json. The output is committed; implementations in
every language load it in their tests. Each invalid fixture breaks exactly
one rule of a valid one, so a schema or validator that is too loose fails a
specific, named case. Re-run after changing PROTOCOL.md and commit the diff.
"""
import base64
import copy
import hashlib
import hmac
import json
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "fixtures")

DEV = "0193f6c2-7a10-7b44-9d92-001122334455"
GW = "0193f6c2-7a10-7c00-8a00-00000000aa01"
USER = "0193f6c2-7a10-7d11-b222-000000000001"
CMD = "0193f6c2-8b20-7e33-a444-556677889900"
TS = 1795780800000

# --- tickets (PROTOCOL.md §6) ----------------------------------------------


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def claims_json(claims: dict) -> str:
    # Compact, keys in this fixed order. Verifiers MAC the received text and
    # never re-serialise, so the order only matters to minting.
    order = ["sub", "dev", "scp", "iat", "exp", "ep"]
    return json.dumps({k: claims[k] for k in order}, separators=(",", ":"))


def mint(key: bytes, claims: dict) -> str:
    body = "LT1." + b64url(claims_json(claims).encode())
    mac = hmac.new(key, body.encode(), hashlib.sha256).digest()
    return body + "." + b64url(mac)


def raw_claims(claims: dict) -> str:
    return claims_json(claims)


def mint_raw(key: bytes, raw: str) -> str:
    body = "LT1." + b64url(raw.encode())
    return body + "." + b64url(hmac.new(key, body.encode(), hashlib.sha256).digest())


def noncanonical(ticket: str) -> str:
    """Set a non-zero unused bit in the MAC's last base64url character (same bytes for lenient decoders)."""
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
    head, last = ticket[:-1], ticket[-1]
    return head + alphabet[alphabet.index(last) | 1]


def tickets():
    key = bytes(range(32))
    base = {"sub": "u:" + USER, "dev": DEV, "scp": "control", "iat": 1795780800, "exp": 1795780800 + 7 * 86400, "ep": 3}
    admin = dict(base, scp="admin", exp=base["iat"] + 12 * 3600)
    gw = dict(base, sub="g:" + GW, scp="admin", exp=base["iat"] + 12 * 3600)
    good = mint(key, base)
    parts = good.split(".")
    tampered_claims = dict(base, scp="admin")
    forged = "LT1." + b64url(claims_json(tampered_claims).encode()) + "." + parts[2]
    now = base["iat"] + 3600
    vectors = {
        "comment": "PROTOCOL.md §6 vectors. key is hex. Verify with device=dev, epoch=ep, now (unix s) unless the case overrides them.",
        "key": key.hex(),
        "device": DEV,
        "epoch": 3,
        "now": now,
        "valid": [
            {"name": "control_user", "claims": base, "ticket": good},
            {"name": "admin_user", "claims": admin, "ticket": mint(key, admin)},
            {"name": "gateway_admin", "claims": gw, "ticket": mint(key, gw)},
        ],
        "invalid": [
            {"name": "bad_mac", "ticket": parts[0] + "." + parts[1] + "." + b64url(b"\0" * 32), "reason": "mac"},
            {"name": "claims_changed_after_signing", "ticket": forged, "reason": "mac"},
            {"name": "wrong_key", "ticket": mint(bytes(32), base), "reason": "mac"},
            {"name": "expired", "ticket": mint(key, dict(base, exp=now - 1)), "reason": "expired"},
            {"name": "old_epoch", "ticket": mint(key, dict(base, ep=2)), "reason": "epoch"},
            {"name": "other_device", "ticket": mint(key, dict(base, dev="0193f6c2-7a10-7b44-9d92-0011223344ff")), "reason": "device"},
            {"name": "control_lifetime_too_long", "ticket": mint(key, dict(base, exp=base["iat"] + 7 * 86400 + 1)), "reason": "lifetime"},
            {"name": "admin_lifetime_too_long", "ticket": mint(key, dict(admin, exp=admin["iat"] + 12 * 3600 + 1)), "reason": "lifetime"},
            {"name": "issued_in_future", "ticket": mint(key, dict(base, iat=now + 600, exp=now + 3600)), "reason": "not_yet_valid"},
            {"name": "wrong_prefix", "ticket": "LT2" + good[3:], "reason": "format"},
            {"name": "missing_part", "ticket": parts[0] + "." + parts[1], "reason": "format"},
            {"name": "not_base64", "ticket": "LT1.!!!." + parts[2], "reason": "format"},
            {"name": "too_long", "ticket": good + "A" * 600, "reason": "format"},
            {"name": "bad_scope", "ticket": mint(key, dict(base, scp="root")), "reason": "claims"},
            # A lenient base64 decoder ignores the 2 unused low bits of the last character;
            # verifiers must reject the non-canonical form (one ticket, one encoding).
            {"name": "noncanonical_mac", "ticket": noncanonical(good), "reason": "format"},
            {"name": "duplicate_claim_key", "ticket": mint_raw(key, raw_claims(base).replace('"ep":3', '"ep":3,"ep":3')),
             "reason": "claims"},
            {"name": "exponent_number", "ticket": mint_raw(key, raw_claims(base).replace(str(base["iat"]), "1.7957808e9")),
             "reason": "claims"},
        ],
        "rules": {
            "max_length": 512,
            "max_lifetime_s": {"control": 604800, "admin": 43200},
            "clock_skew_s": 300,
            "comment": "iat may be at most clock_skew_s in the future; exp must be > now; ep must equal the device's ticket epoch. Both base64url parts must be canonical (unused trailing bits zero). Claims: exactly six distinct keys; iat, exp, ep plain non-negative integers (no fraction, exponent or sign).",
        },
    }
    return vectors


# --- message examples ---------------------------------------------------------

COMMAND = {
    "v": 1, "id": CMD, "ts": TS, "exp": TS + 15000,
    "src": {"kind": "user", "id": USER},
    "op": "light.set", "target": "living_main",
    "args": {"power": True, "brightness": 75, "transitionMs": 400},
}

VALID = {
    "command": {
        "light_set": COMMAND,
        "light_set_color": dict(COMMAND, args={"red": 255, "green": 120, "blue": 0}),
        "light_toggle": dict(COMMAND, args={"toggle": True}),
        "light_effect": dict(COMMAND, op="light.effect", args={"effect": "rainbow", "speedMs": 2000}),
        "light_effect_stop": dict(COMMAND, op="light.effect", args={"effect": "stop"}),
        "group_set": dict(COMMAND, op="group.set", target="downstairs", args={"power": False}),
        "room_set": dict(COMMAND, op="room.set", target="living", args={"brightness": 40}),
        "scene_activate": {k: v for k, v in dict(COMMAND, op="scene.activate", target="evening").items() if k != "args"},
        "device_status": {k: v for k, v in dict(COMMAND, op="device.status").items() if k not in ("target", "args")},
        "device_restart": {k: v for k, v in dict(COMMAND, op="device.restart").items() if k not in ("target", "args")},
        "config_upsert_scene": dict(COMMAND, op="config.apply", target="scene",
                                    args={"baseRevision": 42, "action": "upsert", "doc": {"id": "evening", "name": "Evening"}}),
        "config_delete_light": dict(COMMAND, op="config.apply", target="light",
                                    args={"baseRevision": 42, "action": "delete", "id": "old_lamp"}),
        "config_cloud_epoch": dict(COMMAND, op="config.apply", target="cloud",
                                   args={"baseRevision": 0, "action": "upsert", "doc": {"ticketEpoch": 4}}),
    },
    "command-result": {
        "applied": {"v": 1, "id": CMD, "device": DEV, "status": "applied", "ts": TS + 412},
        "rejected": {"v": 1, "id": CMD, "device": DEV, "status": "rejected", "ts": TS + 30,
                     "code": "unsupported_property", "message": "light has no color"},
        "conflict": {"v": 1, "id": CMD, "device": DEV, "status": "rejected", "ts": TS + 30, "code": "conflict", "revision": 43},
        "failed": {"v": 1, "id": CMD, "device": DEV, "status": "failed", "ts": TS + 30, "code": "hardware_error"},
        "expired": {"v": 1, "id": CMD, "device": DEV, "status": "expired", "ts": TS + 30},
    },
    "state": {
        "two_lights": {
            "v": 1, "device": DEV, "ts": TS, "rev": 118,
            "lights": [
                {"id": "living_main", "type": "rgb", "available": True,
                 "output": {"power": True, "brightness": 75, "red": 255, "green": 180, "blue": 90}, "effect": None},
                {"id": "porch", "type": "switch", "available": False, "output": {"power": False}},
            ],
            "inputs": [{"id": "in1", "state": False}],
        },
        "no_lights": {"v": 1, "device": DEV, "ts": TS, "rev": 0, "lights": []},
    },
    "config-reported": {
        "kc868": {
            "v": 1, "device": DEV, "rev": 42, "ts": TS, "fw": "1.1.0", "board": "kincony_kc868_ap",
            "controllerId": "lumen-0798", "ip": "192.168.1.42",
            "caps": {"ota": True, "backup": True},
            "lights": [
                {"id": "living_main", "name": "Living ceiling", "type": "rgb", "roomId": "living",
                 "supports": {"brightness": True, "color": True, "white": False, "colorTemperature": False,
                              "effects": ["fade", "rainbow"]}},
                {"id": "desk", "name": "Desk", "type": "cct",
                 "supports": {"brightness": True, "color": False, "white": False, "colorTemperature": True,
                              "kelvinMin": 2700, "kelvinMax": 6500, "effects": []}},
            ],
            "rooms": [{"id": "living", "name": "Living room"}],
            "groups": [{"id": "downstairs", "name": "Downstairs", "members": ["living_main", "desk"]}],
            "scenes": [{"id": "evening", "name": "Evening"}],
            "automations": [{"id": "evening_lights", "name": "Evening lights", "enabled": True}],
        },
    },
    "ticket-claims": {
        "control": {"sub": "u:" + USER, "dev": DEV, "scp": "control", "iat": 1795780800, "exp": 1796385600, "ep": 3},
        "gateway": {"sub": "g:" + GW, "dev": DEV, "scp": "admin", "iat": 1795780800, "exp": 1795824000, "ep": 0},
    },
    "register-request": {
        "kc868": {"v": 1, "claimToken": "lct_" + "A" * 43, "fw": "1.1.0",
                  "hw": {"model": "kc868-ap", "board": "kincony_kc868_ap", "mac": "a0:b7:65:12:07:98", "chip": "esp32"},
                  "controllerId": "lumen-0798", "url": "http://192.168.1.42"},
        "via_gateway": {"v": 1, "claimToken": "lct_" + "B" * 43, "fw": "1.1.0",
                        "hw": {"model": "generic-esp32", "mac": "24:0a:c4:00:e7:04"},
                        "controllerId": "lumen-e704", "url": "http://192.168.1.43"},
    },
    "register-response": {
        "child": {"v": 1, "deviceId": DEV, "localKey": "k" * 43, "ticketEpoch": 0, "gatewayId": GW},
    },
    "gateway-register-request": {
        "pi": {"v": 1, "claimToken": "lct_" + "C" * 43, "setupProof": "K7QM-2XRA", "version": "1.0.0",
               "hw": {"model": "raspberry-pi-5", "mac": "d8:3a:dd:00:00:01", "chip": "bcm2712"}},
    },
    "gateway-register-response": {
        "ok": {"v": 1, "gatewayId": GW,
               "mqtt": {"url": "mqtts://mqtt.lumen.example:8883", "username": "g:" + GW, "password": "p" * 43}},
    },
    "children": {
        "two": {"v": 1, "gateway": GW, "ts": TS, "children": [
            {"device": DEV, "routeEpoch": 3, "controllerId": "lumen-0798"},
            {"device": "0193f6c2-7a10-7b44-9d92-0011223344aa", "routeEpoch": 1}]},
        "none": {"v": 1, "gateway": GW, "ts": TS, "children": []},
    },
}


def without(d, *keys):
    return {k: v for k, v in d.items() if k not in keys}


def with_args(base, **args):
    return dict(base, args=args)


C = COMMAND
INVALID = {
    "command": {
        "version_2": dict(C, v=2),
        "missing_exp": without(C, "exp"),
        "bad_id": dict(C, id="not-a-uuid"),
        "unknown_op": dict(C, op="light.blink"),
        "unknown_field": dict(C, extra=1),
        "brightness_over_100": with_args(C, brightness=101),
        "brightness_float": with_args(C, brightness=50.5),
        "red_over_255": with_args(C, red=256),
        "kelvin_too_low": with_args(C, colorTemperature=999),
        "transition_too_long": with_args(C, transitionMs=600001),
        "power_and_toggle": with_args(C, power=True, toggle=True),
        "empty_args": with_args(C),
        "unknown_arg": with_args(C, gpio=5),
        "light_set_without_target": without(C, "target"),
        "target_uppercase": dict(C, target="Living"),
        "target_too_long": dict(C, target="a" * 49),
        "room_set_color": dict(C, op="room.set", target="living", args={"red": 1}),
        "status_with_target": dict(without(C, "args"), op="device.status"),
        "effect_missing_name": dict(C, op="light.effect", args={"speedMs": 200}),
        "config_unknown_kind": dict(C, op="config.apply", target="board",
                                    args={"baseRevision": 1, "action": "upsert", "doc": {}}),
        "config_upsert_without_doc": dict(C, op="config.apply", target="scene", args={"baseRevision": 1, "action": "upsert"}),
        "config_delete_with_doc": dict(C, op="config.apply", target="scene",
                                       args={"baseRevision": 1, "action": "delete", "id": "x", "doc": {}}),
        "src_kind_unknown": dict(C, src={"kind": "cloud", "id": "x"}),
        "ts_seconds_not_ms": dict(C, ts=1795780800),
    },
    "command-result": {
        "rejected_without_code": without(VALID["command-result"]["rejected"], "code"),
        "rejected_unknown_code": dict(VALID["command-result"]["rejected"], code="nope"),
        "applied_with_code": dict(VALID["command-result"]["applied"], code="invalid"),
        "unknown_status": dict(VALID["command-result"]["applied"], status="done"),
        "message_too_long": dict(VALID["command-result"]["rejected"], message="x" * 161),
    },
    "state": {
        "requested_not_output": {**VALID["state"]["no_lights"], "lights": [
            {"id": "a", "type": "dimmer", "available": True, "state": {"power": True}}]},
        "unknown_light_type": {**VALID["state"]["no_lights"], "lights": [
            {"id": "a", "type": "laser", "available": True, "output": {"power": True}}]},
        "output_without_power": {**VALID["state"]["no_lights"], "lights": [
            {"id": "a", "type": "dimmer", "available": True, "output": {"brightness": 3}}]},
        "too_many_lights": {**VALID["state"]["no_lights"], "lights": [
            {"id": f"l{i}", "type": "switch", "available": True, "output": {"power": False}} for i in range(65)]},
        "missing_rev": without(VALID["state"]["no_lights"], "rev"),
    },
    "config-reported": {
        "light_without_supports": {**VALID["config-reported"]["kc868"], "lights": [
            {"id": "a", "name": "A", "type": "dimmer"}]},
        "bad_fw_version": dict(VALID["config-reported"]["kc868"], fw="latest"),
        "pins_leaked": dict(VALID["config-reported"]["kc868"], pins={"pca9685": 21}),
        "name_too_long": {**VALID["config-reported"]["kc868"], "rooms": [{"id": "r", "name": "x" * 65}]},
    },
    "ticket-claims": {
        "bad_subject_prefix": dict(VALID["ticket-claims"]["control"], sub="x:" + USER),
        "bad_scope": dict(VALID["ticket-claims"]["control"], scp="root"),
        "ms_timestamps": dict(VALID["ticket-claims"]["control"], iat=1795780800000),
        "missing_epoch": without(VALID["ticket-claims"]["control"], "ep"),
        "extra_claim": dict(VALID["ticket-claims"]["control"], role="owner"),
    },
    "register-request": {
        "bad_claim_token": dict(VALID["register-request"]["kc868"], claimToken="abc"),
        "bad_mac": {**VALID["register-request"]["kc868"], "hw": {"model": "x", "mac": "A0-B7-65-12-07-98"}},
        "https_lan_url": dict(VALID["register-request"]["via_gateway"], url="https://example.com"),
        "missing_url": without(VALID["register-request"]["kc868"], "url"),
        "secret_field": dict(VALID["register-request"]["kc868"], password="hunter2"),
    },
    "register-response": {
        "controller_gets_mqtt": dict(VALID["register-response"]["child"],
                                     mqtt={"url": "mqtts://x:8883", "username": "d:" + DEV, "password": "p" * 43}),
        "missing_gateway": without(VALID["register-response"]["child"], "gatewayId"),
        "short_local_key": dict(VALID["register-response"]["child"], localKey="short"),
    },
    "gateway-register-request": {
        "missing_setup_proof": without(VALID["gateway-register-request"]["pi"], "setupProof"),
        "setup_code_with_ambiguous_letters": dict(VALID["gateway-register-request"]["pi"], setupProof="IL0U-1234"),
        "setup_code_too_short": dict(VALID["gateway-register-request"]["pi"], setupProof="K7QM"),
        "extra_field": dict(VALID["gateway-register-request"]["pi"], mqttPassword="x"),
    },
    "gateway-register-response": {
        "plain_mqtt_url": {**VALID["gateway-register-response"]["ok"],
                           "mqtt": {"url": "mqtt://broker:1883", "username": "g:" + GW, "password": "p" * 43}},
        "device_username": {**VALID["gateway-register-response"]["ok"],
                            "mqtt": {"url": "mqtts://x:8883", "username": "d:" + DEV, "password": "p" * 43}},
        "missing_mqtt": without(VALID["gateway-register-response"]["ok"], "mqtt"),
    },
    "children": {
        "epoch_zero": {**VALID["children"]["none"], "children": [{"device": DEV, "routeEpoch": 0}]},
        "duplicate_child": {**VALID["children"]["none"], "children": [
            {"device": DEV, "routeEpoch": 1}, {"device": DEV, "routeEpoch": 1}]},
    },
}


def write(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)
        f.write("\n")


def main():
    for name in os.listdir(OUT) if os.path.isdir(OUT) else []:
        p = os.path.join(OUT, name)
        if os.path.isdir(p):
            shutil.rmtree(p)
    for schema, cases in VALID.items():
        for case, obj in cases.items():
            write(os.path.join(OUT, schema, "valid", case + ".json"), copy.deepcopy(obj))
    for schema, cases in INVALID.items():
        for case, obj in cases.items():
            write(os.path.join(OUT, schema, "invalid", case + ".json"), copy.deepcopy(obj))
    write(os.path.join(OUT, "tickets.json"), tickets())
    n_valid = sum(len(c) for c in VALID.values())
    n_invalid = sum(len(c) for c in INVALID.values())
    print(f"wrote {n_valid} valid, {n_invalid} invalid fixtures and tickets.json")


if __name__ == "__main__":
    main()
