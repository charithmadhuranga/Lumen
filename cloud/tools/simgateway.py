#!/usr/bin/env python3
"""A protocol-correct simulated Lumen Gateway with simulated controllers (P4).

Used by the live messaging tests and as the reference for the Go gateway's
cloud side (P6). It registers through the real API (claim tokens, setup code,
HTTP Basic), connects to the broker as `g:{id}` over TLS with a Last Will,
publishes availability, config and state for its children, and answers
commands: checks expiry and duplicates, applies them to simulated lights and
publishes the result and the new state (PROTOCOL.md §3-5).

    uv run python tools/simgateway.py --children 2      # runs until Ctrl-C
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import contextlib
import hashlib
import json
import os
import socket
import ssl
import time
import uuid
from dataclasses import dataclass, field
from typing import Any

import aiomqtt
import httpx

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CA = os.path.join(HERE, "dev-certs", "ca.pem")
SETUP_CODE = "K7QM2XRA"


def _ms() -> int:
    return int(time.time() * 1000)


def config_for(device_id: str, controller_id: str) -> dict[str, Any]:
    return {
        "v": 1,
        "device": device_id,
        "rev": 1,
        "ts": _ms(),
        "fw": "1.1.0",
        "board": "kincony_kc868_ap",
        "controllerId": controller_id,
        "ip": "192.168.1.42",
        "caps": {"ota": True, "backup": True},
        "lights": [
            {
                "id": "living_main",
                "name": "Living ceiling",
                "type": "rgb",
                "roomId": "living",
                "supports": {
                    "brightness": True,
                    "color": True,
                    "white": False,
                    "colorTemperature": False,
                    "effects": ["fade", "rainbow"],
                },
            },
            {
                "id": "desk",
                "name": "Desk",
                "type": "cct",
                "roomId": "living",
                "supports": {
                    "brightness": True,
                    "color": False,
                    "white": False,
                    "colorTemperature": True,
                    "kelvinMin": 2700,
                    "kelvinMax": 6500,
                    "effects": [],
                },
            },
            {
                "id": "porch",
                "name": "Porch",
                "type": "switch",
                "supports": {
                    "brightness": False,
                    "color": False,
                    "white": False,
                    "colorTemperature": False,
                    "effects": [],
                },
            },
        ],
        "rooms": [{"id": "living", "name": "Living room"}],
        "groups": [{"id": "downstairs", "name": "Downstairs", "members": ["living_main", "desk"]}],
        "scenes": [{"id": "evening", "name": "Evening"}],
        "automations": [],
    }


@dataclass
class SimChild:
    device_id: str
    controller_id: str
    local_key: str
    lights: dict[str, dict[str, Any]] = field(
        default_factory=lambda: {
            "living_main": {
                "type": "rgb",
                "output": {"power": False, "brightness": 0, "red": 255, "green": 255, "blue": 255},
            },
            "desk": {"type": "cct", "output": {"power": False, "brightness": 0, "colorTemperature": 4000}},
            "porch": {"type": "switch", "output": {"power": False}},
        }
    )
    effect: dict[str, str | None] = field(default_factory=dict)
    applied: list[str] = field(default_factory=list)  # command ids executed, in order

    def state(self) -> dict[str, Any]:
        return {
            "v": 1,
            "device": self.device_id,
            "ts": _ms(),
            "rev": 1,
            "lights": [
                {
                    "id": i,
                    "type": light["type"],
                    "available": True,
                    "output": dict(light["output"]),
                    "effect": self.effect.get(i),
                }
                for i, light in self.lights.items()
            ],
        }


class SimGateway:
    def __init__(
        self,
        api: str = "http://127.0.0.1:8800",
        ca: str = DEFAULT_CA,
        broker: str = "127.0.0.1",
        port: int = 28883,
        latency_s: float = 0.0,
        keepalive: int = 10,
    ) -> None:
        self.api = api.rstrip("/") + "/api/v1"
        self.ca, self.broker, self.port = ca, broker, port
        self.latency_s, self.keepalive = latency_s, keepalive
        self.gateway_id = self.secret = ""
        self.children: dict[str, SimChild] = {}
        self.received: list[str] = []  # every command id delivered (duplicates included)
        self.client: aiomqtt.Client | None = None
        self._task: asyncio.Task[None] | None = None
        self._ready = asyncio.Event()

    # --- registration through the API -------------------------------------------------------

    async def register(self, token: str, home_id: str, children: int = 1) -> None:
        auth = {"Authorization": f"Bearer {token}"}
        async with httpx.AsyncClient(timeout=10) as http:
            proof = hashlib.sha256(SETUP_CODE.encode()).hexdigest()
            claim = (
                await http.post(
                    f"{self.api}/homes/{home_id}/gateway-claims", json={"setupProofHash": proof}, headers=auth
                )
            ).json()["data"]["claimToken"]
            mac = "d8:3a:dd:" + ":".join(f"{b:02x}" for b in os.urandom(3))
            r = await http.post(
                f"{self.api}/gateway/register",
                json={
                    "v": 1,
                    "claimToken": claim,
                    "setupProof": SETUP_CODE,
                    "hw": {"model": "simgateway", "mac": mac},
                    "version": "1.0.0",
                },
            )
            r.raise_for_status()
            reg = r.json()["data"]
            self.gateway_id, self.secret = reg["gatewayId"], reg["mqtt"]["password"]
            basic = base64.b64encode(f"g:{self.gateway_id}:{self.secret}".encode()).decode()
            for n in range(children):
                claim = (
                    await http.post(
                        f"{self.api}/homes/{home_id}/claims", json={"gatewayId": self.gateway_id}, headers=auth
                    )
                ).json()["data"]["claimToken"]
                controller_id = f"lumen-sim{n}"
                r = await http.post(
                    f"{self.api}/gateways/{self.gateway_id}/children/register",
                    json={
                        "v": 1,
                        "claimToken": claim,
                        "fw": "1.1.0",
                        "controllerId": controller_id,
                        "url": f"http://192.168.1.{50 + n}",
                        "hw": {"model": "kc868-ap", "mac": "a0:b7:65:" + ":".join(f"{b:02x}" for b in os.urandom(3))},
                    },
                    headers={"Authorization": f"Basic {basic}"},
                )
                r.raise_for_status()
                child = r.json()["data"]
                self.children[child["deviceId"]] = SimChild(child["deviceId"], controller_id, child["localKey"])

    # --- broker session ------------------------------------------------------------------------

    async def start(self) -> None:
        self._task = asyncio.create_task(self._run())
        await asyncio.wait_for(self._ready.wait(), 10)

    async def _publish(self, topic: str, body: dict[str, Any] | str, retain: bool = False) -> None:
        assert self.client is not None
        data = body.encode() if isinstance(body, str) else json.dumps(body, separators=(",", ":")).encode()
        await self.client.publish(topic, data, qos=1, retain=retain)

    async def _run(self) -> None:
        cid = f"g:{self.gateway_id}"
        will = aiomqtt.Will(f"lumen/v1/g/{self.gateway_id}/availability", b"offline", qos=1, retain=True)
        async with aiomqtt.Client(
            self.broker,
            self.port,
            identifier=cid,
            username=cid,
            password=self.secret,
            tls_context=ssl.create_default_context(cafile=self.ca),
            will=will,
            protocol=aiomqtt.ProtocolVersion.V5,
            keepalive=self.keepalive,
            timeout=10,
        ) as client:
            self.client = client
            await client.subscribe(f"lumen/v1/g/{self.gateway_id}/children", qos=1)
            for dev in self.children:
                await client.subscribe(f"lumen/v1/d/{dev}/cmd", qos=1)
            await self._publish(f"lumen/v1/g/{self.gateway_id}/availability", "online", retain=True)
            for dev, child in self.children.items():
                await self._publish(f"lumen/v1/d/{dev}/availability", "online", retain=True)
                await self._publish(
                    f"lumen/v1/d/{dev}/config/reported", config_for(dev, child.controller_id), retain=True
                )
                await self._publish(f"lumen/v1/d/{dev}/state", child.state(), retain=True)
            self._ready.set()
            async for message in client.messages:
                topic = str(message.topic)
                if topic.endswith("/cmd") and isinstance(message.payload, bytes):
                    asyncio.create_task(self._command(topic.split("/")[3], message.payload))  # noqa: RUF006

    async def _command(self, device_id: str, payload: bytes) -> None:
        cmd = json.loads(payload)
        self.received.append(cmd["id"])
        child = self.children.get(device_id)
        if child is None:
            return
        if self.latency_s:
            await asyncio.sleep(self.latency_s)
        result: dict[str, Any] = {"v": 1, "id": cmd["id"], "device": device_id}
        if cmd["id"] in child.applied:
            return  # QoS 1 duplicate: executed once, result already sent
        if _ms() > cmd["exp"]:
            result["status"] = "expired"
        else:
            status, code = self._apply(child, cmd)
            result["status"] = status
            if code:
                result["code"] = code
            if status == "applied":
                child.applied.append(cmd["id"])
        result["ts"] = _ms()
        await self._publish(f"lumen/v1/d/{device_id}/cmd/result", result)
        if result["status"] == "applied":
            await self._publish(f"lumen/v1/d/{device_id}/state", child.state(), retain=True)

    def _apply(self, child: SimChild, cmd: dict[str, Any]) -> tuple[str, str | None]:
        op, target, args = cmd["op"], cmd.get("target"), cmd.get("args") or {}
        if op in ("light.set", "light.effect") and target not in child.lights:
            return "rejected", "unknown_target"
        if op == "light.set":
            out = child.lights[target]["output"]
            if args.get("toggle"):
                out["power"] = not out["power"]
            for key in ("power", "brightness", "red", "green", "blue", "white", "colorTemperature"):
                if key in args and (key in out or key == "power"):
                    out[key] = args[key]
            return "applied", None
        if op == "light.effect":
            child.effect[target] = None if args["effect"] in ("stop", "none") else args["effect"]
            return "applied", None
        if op in ("scene.activate", "group.set", "room.set", "device.status"):
            return "applied", None
        return "rejected", "unsupported"

    async def stop(self, graceful: bool = True) -> None:
        """graceful=False ends the TCP connection without DISCONNECT, so the broker fires the Last Will."""
        if self.client is not None:
            if graceful:
                for dev in self.children:
                    await self._publish(f"lumen/v1/d/{dev}/availability", "offline", retain=True)
                await self._publish(f"lumen/v1/g/{self.gateway_id}/availability", "offline", retain=True)
            else:
                # Like a killed process: the TCP connection ends without an MQTT DISCONNECT, so the
                # broker publishes the Last Will. shutdown() (not close()) keeps the descriptor valid
                # until the MQTT library cleans up; closing it here would let the OS reuse the number.
                sock = self.client._client.socket()
                if sock is not None:
                    with contextlib.suppress(OSError):
                        sock.shutdown(socket.SHUT_RDWR)
        if self._task is not None:
            self._task.cancel()
            with contextlib.suppress(asyncio.CancelledError, aiomqtt.MqttError, OSError, ssl.SSLError):
                await self._task
        self.client = None


async def _main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--api", default="http://127.0.0.1:8800")
    parser.add_argument("--email", default="sim@example.com")
    parser.add_argument("--password", default="simulated gateway 1")
    parser.add_argument("--children", type=int, default=1)
    parser.add_argument("--latency", type=float, default=0.0)
    a = parser.parse_args()
    async with httpx.AsyncClient(base_url=a.api.rstrip("/") + "/api/v1", timeout=10) as http:
        r = await http.post("/auth/login", json={"email": a.email, "password": a.password})
        if r.status_code != 200:
            r = await http.post("/auth/register", json={"email": a.email, "password": a.password})
        token = r.json()["data"]["accessToken"]
        home = (
            await http.post(
                "/homes",
                json={"name": f"Sim {uuid.uuid4().hex[:4]}", "timezone": "UTC"},
                headers={"Authorization": f"Bearer {token}"},
            )
        ).json()["data"]["id"]
    sim = SimGateway(a.api, latency_s=a.latency)
    await sim.register(token, home, a.children)
    await sim.start()
    print(f"simgateway {sim.gateway_id} in home {home} with controllers {list(sim.children)}; Ctrl-C to stop")
    try:
        await asyncio.Event().wait()
    finally:
        await sim.stop()


if __name__ == "__main__":
    with contextlib.suppress(KeyboardInterrupt):
        asyncio.run(_main())
