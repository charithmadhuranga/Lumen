"""End-to-end messaging against the compose stack (`make up`; the replica test
also needs `docker compose --profile replicas up -d`): app -> API -> cloud MQTT
bridge -> EMQX -> simulated gateway -> results/state -> API -> events WebSocket.

Recorded as "local docker" in STATUS. Skipped when the stack is not running.
"""

from __future__ import annotations

import asyncio
import json
import os
import socket
import ssl
import statistics
import time
from collections.abc import AsyncIterator
from typing import Any

import aiomqtt
import httpx
import pytest
import websockets
from simgateway import SimGateway  # type: ignore[import-not-found]  # tools/ is on sys.path (conftest)

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CA = os.path.join(HERE, "dev-certs", "ca.pem")
API, API2 = "http://127.0.0.1:8800", "http://127.0.0.1:8802"
SERVICE_PASSWORD = "dev-insecure-change-me-0123456789abcdef"  # .env.example development value


def _port_open(port: int) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            return True
    except OSError:
        return False


pytestmark = pytest.mark.skipif(
    not (_port_open(8800) and _port_open(28883) and os.path.exists(CA)), reason="compose stack not running (make up)"
)
LIGHT_ON = {"op": "light.set", "target": "living_main", "args": {"power": True, "brightness": 60}}


class Live:
    def __init__(self, http: httpx.AsyncClient, token: str, home: str) -> None:
        self.http, self.token, self.home = http, token, home
        self.auth = {"Authorization": f"Bearer {token}"}

    async def command(self, dev: str, body: dict[str, Any], base: str = API) -> httpx.Response:
        return await self.http.post(f"{base}/api/v1/devices/{dev}/commands", json=body, headers=self.auth)

    async def get_command(self, cid: str) -> dict[str, Any]:
        r = await self.http.get(f"{API}/api/v1/commands/{cid}", headers=self.auth)
        return r.json()["data"]  # type: ignore[no-any-return]

    async def wait_status(self, cid: str, status: str, within: float = 5.0) -> dict[str, Any]:
        deadline = time.monotonic() + within
        while True:
            out = await self.get_command(cid)
            if out["status"] == status or time.monotonic() > deadline:
                return out
            await asyncio.sleep(0.1)


@pytest.fixture
async def live() -> AsyncIterator[Live]:
    async with httpx.AsyncClient(timeout=10) as http:
        email, pw = "live-messaging@example.com", "live messaging phrase 1"
        r = await http.post(f"{API}/api/v1/auth/login", json={"email": email, "password": pw})
        if r.status_code != 200:
            r = await http.post(f"{API}/api/v1/auth/register", json={"email": email, "password": pw})
        token = r.json()["data"]["accessToken"]
        home = (
            await http.post(
                f"{API}/api/v1/homes",
                json={"name": "Live", "timezone": "UTC"},
                headers={"Authorization": f"Bearer {token}"},
            )
        ).json()["data"]["id"]
        yield Live(http, token, home)
        await http.delete(f"{API}/api/v1/homes/{home}", headers={"Authorization": f"Bearer {token}"})


async def wait_ready(live: Live, gw: SimGateway) -> None:
    """Until every child is online and its configuration is stored (ingested asynchronously)."""
    deadline = time.monotonic() + 10
    for dev in gw.children:
        while True:
            st = (await live.http.get(f"{API}/api/v1/devices/{dev}/status", headers=live.auth)).json()["data"]
            if st["online"] and st["configRev"] is not None:
                break
            assert time.monotonic() < deadline, f"{dev} not ready: {st}"
            await asyncio.sleep(0.05)


@pytest.fixture
async def sim(live: Live) -> AsyncIterator[SimGateway]:
    gw = SimGateway(API, keepalive=5)
    await gw.register(live.token, live.home, 1)
    await gw.start()
    await wait_ready(live, gw)
    yield gw
    await gw.stop()


async def events_ws(token: str, base: str = "ws://127.0.0.1:8800") -> Any:
    ws = await websockets.connect(f"{base}/api/v1/events")
    await ws.send(json.dumps({"type": "auth", "accessToken": token}))
    hello = json.loads(await ws.recv())
    assert hello["event"] == "hello"
    return ws


async def next_event(ws: Any, name: str, within: float = 5.0, **match: Any) -> dict[str, Any]:
    deadline = time.monotonic() + within
    while True:
        msg = json.loads(await asyncio.wait_for(ws.recv(), max(0.05, deadline - time.monotonic())))
        if msg["event"] == name and all(msg["data"].get(k) == v for k, v in match.items()):
            return msg  # type: ignore[no-any-return]


async def test_round_trip_and_latency(live: Live, sim: SimGateway) -> None:
    dev = next(iter(sim.children))
    ws = await events_ws(live.token)
    latencies = []
    try:
        # Warm-up: the first publish after a restart pays for cold broker authorisation caches.
        warm = (await live.command(dev, LIGHT_ON)).json()["data"]["id"]
        await live.wait_status(warm, "applied")
        for i in range(20):
            start = time.perf_counter()
            r = await live.command(dev, {**LIGHT_ON, "args": {"power": True, "brightness": i * 5}})
            assert r.status_code == 202, r.text
            cid = r.json()["data"]["id"]
            # The result and the new state are ingested in parallel, so they may arrive in either order.
            applied_at, brightness = None, None
            while applied_at is None or brightness != i * 5:
                msg = json.loads(await asyncio.wait_for(ws.recv(), 5))
                if (
                    msg["event"] == "command.updated"
                    and msg["data"]["commandId"] == cid
                    and msg["data"]["status"] == "applied"
                ):
                    applied_at = time.perf_counter()
                elif msg["event"] == "device.state" and msg["data"]["deviceId"] == dev:
                    brightness = msg["data"]["lights"][0]["output"]["brightness"]
            latencies.append((applied_at - start) * 1000)
    finally:
        await ws.close()
    p95 = statistics.quantiles(latencies, n=20)[-1]
    print(f"command -> applied over WebSocket: median {statistics.median(latencies):.0f} ms, p95 {p95:.0f} ms")
    assert p95 < 300
    assert len(sim.children[dev].applied) == 21


async def test_duplicate_delivery_executes_once(live: Live, sim: SimGateway) -> None:
    dev = next(iter(sim.children))
    cid = (await live.command(dev, LIGHT_ON)).json()["data"]["id"]
    assert (await live.wait_status(cid, "applied"))["status"] == "applied"
    # Re-deliver the very same command (as QoS 1 may), straight from a service client.
    cmd_body = {
        "v": 1,
        "id": cid,
        "ts": int(time.time() * 1000),
        "exp": int(time.time() * 1000) + 10_000,
        "src": {"kind": "user", "id": "x"},
        **LIGHT_ON,
    }
    async with aiomqtt.Client(
        "127.0.0.1",
        28883,
        identifier="svc:live-dup",
        username="svc:live-dup",
        password=SERVICE_PASSWORD,
        tls_context=ssl.create_default_context(cafile=CA),
    ) as svc:
        await svc.publish(f"lumen/v1/d/{dev}/cmd", json.dumps(cmd_body), qos=1)
    await asyncio.sleep(0.5)
    assert sim.received.count(cid) == 2
    assert sim.children[dev].applied.count(cid) == 1
    assert (await live.get_command(cid))["status"] == "applied"


async def test_expired_command_is_never_applied(live: Live) -> None:
    slow = SimGateway(API, latency_s=2.0, keepalive=5)
    await slow.register(live.token, live.home, 1)
    await slow.start()
    try:
        await wait_ready(live, slow)
        dev = next(iter(slow.children))
        cid = (await live.command(dev, {**LIGHT_ON, "ttlSeconds": 1})).json()["data"]["id"]
        out = await live.wait_status(cid, "expired", within=5)
        assert out["status"] == "expired"
        await asyncio.sleep(2.0)
        assert slow.children[dev].applied == []
        assert (await live.get_command(cid))["status"] == "expired"
    finally:
        await slow.stop()


async def test_offline_controller_is_refused(live: Live, sim: SimGateway) -> None:
    dev = next(iter(sim.children))
    await sim.stop()
    await asyncio.sleep(0.5)
    r = await live.command(dev, LIGHT_ON)
    assert r.status_code == 409
    assert r.json()["error"]["code"] == "DEVICE_OFFLINE"


async def test_gateway_crash_marks_children_offline_via_last_will(live: Live, sim: SimGateway) -> None:
    dev = next(iter(sim.children))
    ws = await events_ws(live.token)
    try:
        start = time.monotonic()
        await sim.stop(graceful=False)  # connection ends without DISCONNECT: the broker publishes the Last Will
        await next_event(ws, "device.offline", within=15, deviceId=sim.gateway_id)
        await next_event(ws, "device.offline", within=2, deviceId=dev)
        elapsed = time.monotonic() - start
    finally:
        await ws.close()
    print(f"gateway crash -> device.offline after {elapsed:.1f} s")
    # A dropped connection is seen at once. (A silent loss, e.g. power cut, waits for the
    # keepalive: measured 10.0 s with keepalive 5 s on EMQX 5.8.9, i.e. <= 2 x keepalive.)
    assert elapsed <= 3.0
    r = await live.command(dev, LIGHT_ON)
    assert r.status_code == 409


async def test_children_list_is_retained_for_the_gateway(live: Live, sim: SimGateway) -> None:
    topic = f"lumen/v1/g/{sim.gateway_id}/children"
    async with aiomqtt.Client(
        "127.0.0.1",
        28883,
        identifier="svc:live-children",
        username="svc:live-children",
        password=SERVICE_PASSWORD,
        tls_context=ssl.create_default_context(cafile=CA),
    ) as svc:
        await svc.subscribe(topic, qos=1)
        msg = await asyncio.wait_for(anext(aiter(svc.messages)), 5)
    body = json.loads(msg.payload)  # type: ignore[arg-type]
    assert msg.retain
    assert [c["device"] for c in body["children"]] == list(sim.children)


@pytest.mark.skipif(not _port_open(8802), reason="replica not running (docker compose --profile replicas up -d)")
async def test_two_replicas_share_the_work(live: Live, sim: SimGateway) -> None:
    dev = next(iter(sim.children))
    ws = await events_ws(live.token, "ws://127.0.0.1:8800")  # events from replica 1
    try:
        ids = []
        for _ in range(10):
            r = await live.command(dev, LIGHT_ON, base=API2)  # commands through replica 2
            assert r.status_code == 202, r.text
            ids.append(r.json()["data"]["id"])
        # Results are ingested in parallel on both replicas: collect them in any order.
        pending = set(ids)
        while pending:
            msg = json.loads(await asyncio.wait_for(ws.recv(), 5))
            if msg["event"] == "command.updated" and msg["data"]["status"] == "applied":
                pending.discard(msg["data"]["commandId"])
    finally:
        await ws.close()
    assert [sim.received.count(cid) for cid in ids] == [1] * 10  # each delivered once, not once per replica
