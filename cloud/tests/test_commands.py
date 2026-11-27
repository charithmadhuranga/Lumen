"""Commands, ingestion, sweeper, children publishing and the event hub, in-process
(test database, fake publisher). The real broker path is in test_messaging_live.py."""

from __future__ import annotations

import asyncio
import json
import time
import uuid
from collections.abc import AsyncIterator
from datetime import UTC, datetime, timedelta
from typing import Any

import pytest
from fastapi import FastAPI
from httpx import AsyncClient
from simgateway import config_for  # type: ignore[import-not-found]  # tools/ is on sys.path (conftest)
from sqlalchemy import update

from app import protocol
from app.models.home import Role
from app.models.messaging import Command, DeviceState
from app.mqtt.bridge import BridgeUnavailable, Publisher
from app.services import ingest
from app.services.events import EventHub, Subscriber
from app.services.ingest import Ingestor
from app.workers.sweeper import expire_commands
from tests.conftest import make_settings
from tests.helpers import data, error
from tests.test_devices import Setup, controller_claim, register_child, setup
from tests.test_homes import add_member, user

pytestmark = pytest.mark.usefixtures("migrated")


class FakePublisher(Publisher):
    def __init__(self) -> None:
        self.connected = True
        self.sent: list[tuple[str, Any, bool, int | None]] = []
        self.fail = False

    async def publish(
        self, topic: str, payload: dict[str, Any] | bytes, *, retain: bool = False, expiry_s: int | None = None
    ) -> None:
        if self.fail:
            raise BridgeUnavailable("down")
        self.sent.append((topic, payload, retain, expiry_s))

    def commands(self) -> list[dict[str, Any]]:
        return [p for t, p, _, _ in self.sent if t.endswith("/cmd")]  # type: ignore[misc]


@pytest.fixture
def pub() -> FakePublisher:
    return FakePublisher()


@pytest.fixture
def app(pub: FakePublisher) -> FastAPI:
    from app.main import create_public_app

    return create_public_app(make_settings(emqx_api_url=""), publisher=pub)


@pytest.fixture
async def c(app: FastAPI, make_client) -> AsyncIterator[AsyncClient]:  # type: ignore[no-untyped-def]
    async for client in make_client(app):
        yield client


def ms() -> int:
    return int(time.time() * 1000)


async def online_controller(app: FastAPI, c: AsyncClient) -> tuple[Setup, str, Ingestor]:
    s = await setup(app, c)
    dev = data(await register_child(c, s, await controller_claim(c, s)), 201)["deviceId"]
    ing = Ingestor(app.state.sessionmaker, app.state.redis)
    await ing.handle(f"lumen/v1/g/{s.gateway_id}/availability", b"online")
    await ing.handle(f"lumen/v1/d/{dev}/availability", b"online")
    await ing.handle(f"lumen/v1/d/{dev}/config/reported", json.dumps(config_for(dev, "lumen-0798")).encode())
    return s, dev, ing


async def send(
    c: AsyncClient,
    s: Setup,
    dev: str,
    body: dict[str, Any],
    headers: dict[str, str] | None = None,
    key: str | None = None,
) -> Any:
    h = dict(headers or s.owner)
    if key:
        h["Idempotency-Key"] = key
    return await c.post(f"/api/v1/devices/{dev}/commands", json=body, headers=h)


LIGHT_ON = {"op": "light.set", "target": "living_main", "args": {"power": True, "brightness": 60}}


# --- sending -----------------------------------------------------------------------------------


async def test_command_is_published_with_expiry_and_protocol_shape(
    app: FastAPI, c: AsyncClient, pub: FakePublisher
) -> None:
    s, dev, _ = await online_controller(app, c)
    out = data(await send(c, s, dev, {**LIGHT_ON, "ttlSeconds": 20}), 202)
    assert out["status"] == "sent"
    topic, payload, retain, expiry = next(x for x in pub.sent if x[0].endswith("/cmd"))
    assert (topic, retain, expiry) == (f"lumen/v1/d/{dev}/cmd", False, 20)
    cmd = protocol.Command.model_validate(payload)
    assert str(cmd.id) == out["id"]
    assert cmd.exp - cmd.ts == 20_000
    assert cmd.src.kind == "user"


async def test_offline_device_is_refused_not_queued(app: FastAPI, c: AsyncClient, pub: FakePublisher) -> None:
    s, dev, ing = await online_controller(app, c)
    await ing.handle(f"lumen/v1/d/{dev}/availability", b"offline")
    error(await send(c, s, dev, LIGHT_ON), 409, "DEVICE_OFFLINE")
    await ing.handle(f"lumen/v1/d/{dev}/availability", b"online")
    await ing.handle(f"lumen/v1/g/{s.gateway_id}/availability", b"offline")  # children go offline with it
    error(await send(c, s, dev, LIGHT_ON), 409, "DEVICE_OFFLINE")
    assert pub.commands() == []


@pytest.mark.parametrize(
    ("body", "status", "code"),
    [
        ({"op": "light.set", "target": "nope", "args": {"power": True}}, 404, "UNKNOWN_TARGET"),
        ({"op": "light.set", "target": "desk", "args": {"red": 10}}, 422, "UNSUPPORTED_PROPERTY"),
        ({"op": "light.set", "target": "desk", "args": {"colorTemperature": 9000}}, 422, "UNSUPPORTED_PROPERTY"),
        ({"op": "light.set", "target": "porch", "args": {"brightness": 5}}, 422, "UNSUPPORTED_PROPERTY"),
        ({"op": "light.effect", "target": "desk", "args": {"effect": "rainbow"}}, 422, "UNSUPPORTED_PROPERTY"),
        ({"op": "scene.activate", "target": "morning"}, 404, "UNKNOWN_TARGET"),
        ({"op": "group.set", "target": "upstairs", "args": {"power": False}}, 404, "UNKNOWN_TARGET"),
        ({"op": "light.set", "target": "living_main", "args": {"brightness": 101}}, 400, "INVALID_COMMAND"),
        ({"op": "light.set", "target": "living_main", "args": {"power": True, "toggle": True}}, 400, "INVALID_COMMAND"),
        ({"op": "light.set", "target": "living_main", "args": {"gpio": 4}}, 400, "INVALID_COMMAND"),
        ({"op": "light.set", "target": "living_main"}, 400, "INVALID_COMMAND"),
        ({"op": "device.status", "target": "x"}, 400, "INVALID_COMMAND"),
        ({"op": "light.blink", "target": "living_main", "args": {}}, 400, "INVALID_COMMAND"),
        (
            {"op": "config.apply", "target": "scene", "args": {"baseRevision": 1, "action": "delete", "id": "x"}},
            400,
            "NOT_AVAILABLE",
        ),
        ({**LIGHT_ON, "ttlSeconds": 61}, 400, "INVALID_REQUEST"),
    ],
)
async def test_command_validation(
    app: FastAPI, c: AsyncClient, pub: FakePublisher, body: dict[str, Any], status: int, code: str
) -> None:
    s, dev, _ = await online_controller(app, c)
    error(await send(c, s, dev, body), status, code)
    assert pub.commands() == []


@pytest.mark.parametrize(
    "body",
    [
        {"op": "light.set", "target": "desk", "args": {"colorTemperature": 3000, "transitionMs": 500}},
        {"op": "light.set", "target": "porch", "args": {"toggle": True}},
        {"op": "light.effect", "target": "living_main", "args": {"effect": "stop"}},
        {"op": "group.set", "target": "downstairs", "args": {"brightness": 20}},
        {"op": "room.set", "target": "living", "args": {"power": False}},
        {"op": "scene.activate", "target": "evening"},
        {"op": "device.status"},
    ],
)
async def test_supported_commands(app: FastAPI, c: AsyncClient, body: dict[str, Any]) -> None:
    s, dev, _ = await online_controller(app, c)
    data(await send(c, s, dev, body), 202)


async def test_roles(app: FastAPI, c: AsyncClient) -> None:
    s, dev, _ = await online_controller(app, c)
    for role, restart_status in ((Role.guest, 403), (Role.member, 403), (Role.admin, 202)):
        uid, h = await user(app, f"cmd-{role.value}@example.com")
        await add_member(app, s.home, uid, role)
        data(await send(c, s, dev, LIGHT_ON, headers=h), 202)
        assert (await send(c, s, dev, {"op": "device.restart"}, headers=h)).status_code == restart_status
    _, outsider = await user(app, "cmd-out@example.com")
    error(await send(c, s, dev, LIGHT_ON, headers=outsider), 404, "NOT_FOUND")


async def test_gateway_and_unconfigured_controller(app: FastAPI, c: AsyncClient) -> None:
    s, _dev, ing = await online_controller(app, c)
    error(await send(c, s, s.gateway_id, LIGHT_ON), 400, "INVALID_REQUEST")
    other = data(
        await register_child(c, s, await controller_claim(c, s), hw={"model": "x", "mac": "a0:b7:65:00:00:09"}), 201
    )
    await ing.handle(f"lumen/v1/d/{other['deviceId']}/availability", b"online")
    error(await send(c, s, other["deviceId"], LIGHT_ON), 409, "CONFIG_UNKNOWN")


async def test_idempotency_key(app: FastAPI, c: AsyncClient, pub: FakePublisher) -> None:
    s, dev, _ = await online_controller(app, c)
    first = data(await send(c, s, dev, LIGHT_ON, key="retry-key-0001"), 202)
    again = data(await send(c, s, dev, LIGHT_ON, key="retry-key-0001"), 202)
    assert first["id"] == again["id"]
    assert len(pub.commands()) == 1
    error(await send(c, s, dev, {"op": "device.status"}, key="retry-key-0001"), 409, "CONFLICT")
    error(await send(c, s, dev, LIGHT_ON, key="bad key!"), 400, "INVALID_REQUEST")


async def test_broker_unavailable_fails_the_command(app: FastAPI, c: AsyncClient, pub: FakePublisher) -> None:
    s, dev, _ = await online_controller(app, c)
    pub.fail = True
    error(await send(c, s, dev, LIGHT_ON), 503, "UNAVAILABLE")
    history = data(await c.get(f"/api/v1/devices/{dev}/commands", headers=s.owner))
    assert [(h["status"], h["code"]) for h in history] == [("failed", "broker_unavailable")]


# --- results, expiry -------------------------------------------------------------------------------


async def result(
    ing: Ingestor, dev: str, cid: str, status: str, code: str | None = None, topic_dev: str | None = None
) -> None:
    body: dict[str, Any] = {"v": 1, "id": cid, "device": dev, "status": status, "ts": ms()}
    if code:
        body["code"] = code
    await ing.handle(f"lumen/v1/d/{topic_dev or dev}/cmd/result", json.dumps(body).encode())


async def test_result_lifecycle(app: FastAPI, c: AsyncClient) -> None:
    s, dev, ing = await online_controller(app, c)
    cid = data(await send(c, s, dev, LIGHT_ON), 202)["id"]
    await result(ing, dev, cid, "received")
    assert data(await c.get(f"/api/v1/commands/{cid}", headers=s.owner))["status"] == "received"
    await result(ing, dev, cid, "applied")
    out = data(await c.get(f"/api/v1/commands/{cid}", headers=s.owner))
    assert out["status"] == "applied"
    assert out["completedAt"] is not None
    before = ingest.stats["result_ignored"]
    await result(ing, dev, cid, "rejected", "invalid")  # after a final state: ignored
    await result(ing, dev, cid, "applied")  # QoS 1 duplicate: ignored
    assert data(await c.get(f"/api/v1/commands/{cid}", headers=s.owner))["status"] == "applied"
    assert ingest.stats["result_ignored"] == before + 2


async def test_rejected_result_keeps_code(app: FastAPI, c: AsyncClient) -> None:
    s, dev, ing = await online_controller(app, c)
    cid = data(await send(c, s, dev, LIGHT_ON), 202)["id"]
    await result(ing, dev, cid, "rejected", "unsupported_property")
    out = data(await c.get(f"/api/v1/commands/{cid}", headers=s.owner))
    assert (out["status"], out["code"]) == ("rejected", "unsupported_property")


async def test_result_for_another_device_is_ignored(app: FastAPI, c: AsyncClient) -> None:
    s, dev, ing = await online_controller(app, c)
    other = data(
        await register_child(c, s, await controller_claim(c, s), hw={"model": "x", "mac": "a0:b7:65:00:00:08"}), 201
    )
    cid = data(await send(c, s, dev, LIGHT_ON), 202)["id"]
    await result(ing, other["deviceId"], cid, "applied")  # the other controller cannot finish this command
    await result(ing, dev, cid, "applied", topic_dev=other["deviceId"])  # payload/topic mismatch: invalid
    assert data(await c.get(f"/api/v1/commands/{cid}", headers=s.owner))["status"] == "sent"


async def test_sweeper_expires_open_commands_once(app: FastAPI, c: AsyncClient) -> None:
    s, dev, ing = await online_controller(app, c)
    cid = data(await send(c, s, dev, LIGHT_ON), 202)["id"]
    async with app.state.sessionmaker() as db:
        await db.execute(update(Command).values(expires_at=datetime.now(UTC) - timedelta(seconds=1)))
        await db.commit()
    assert await expire_commands(app.state.sessionmaker, app.state.redis) == 1
    assert await expire_commands(app.state.sessionmaker, app.state.redis) == 0
    await result(ing, dev, cid, "applied")  # too late
    assert data(await c.get(f"/api/v1/commands/{cid}", headers=s.owner))["status"] == "expired"


# --- ingestion -------------------------------------------------------------------------------------


def state(dev: str, ts: int, power: bool) -> bytes:
    return json.dumps(
        {
            "v": 1,
            "device": dev,
            "ts": ts,
            "rev": 1,
            "lights": [{"id": "porch", "type": "switch", "available": True, "output": {"power": power}}],
        }
    ).encode()


async def test_state_only_moves_forward(app: FastAPI, c: AsyncClient) -> None:
    s, dev, ing = await online_controller(app, c)
    now = ms()
    await ing.handle(f"lumen/v1/d/{dev}/state", state(dev, now, True))
    await ing.handle(f"lumen/v1/d/{dev}/state", state(dev, now - 5000, False))  # late delivery of an older report
    st = data(await c.get(f"/api/v1/devices/{dev}/status", headers=s.owner))
    assert st["online"] is True
    assert st["state"]["lights"][0]["output"] == {"power": True}


@pytest.mark.parametrize(
    "payload",
    [
        b"not json",
        b'{"v":2}',
        b"[]",
        b"",
        b'{"v":1,"device":"00000000-0000-7000-8000-000000000000","ts":1795780800000,"rev":1,"lights":[]}',
    ],
)
async def test_invalid_state_is_dropped(app: FastAPI, c: AsyncClient, payload: bytes) -> None:
    s, dev, ing = await online_controller(app, c)
    before = ingest.stats["invalid"]
    await ing.handle(f"lumen/v1/d/{dev}/state", payload)
    assert ingest.stats["invalid"] == before + 1
    assert data(await c.get(f"/api/v1/devices/{dev}/status", headers=s.owner))["state"] is None


async def test_unknown_devices_and_topics_are_ignored(app: FastAPI, c: AsyncClient) -> None:
    s, _, ing = await online_controller(app, c)
    before = dict(ingest.stats)
    await ing.handle(f"lumen/v1/d/{uuid.uuid4()}/availability", b"online")
    await ing.handle(f"lumen/v1/d/{s.gateway_id}/availability", b"online")  # a gateway on a controller topic
    await ing.handle("lumen/v1/x/y/z", b"{}")
    assert ingest.stats["unknown_device"] == before.get("unknown_device", 0) + 2
    assert ingest.stats["bad_topic"] == before.get("bad_topic", 0) + 1


async def test_config_report_updates_device(app: FastAPI, c: AsyncClient) -> None:
    s, dev, _ = await online_controller(app, c)
    out = data(await c.get(f"/api/v1/devices/{dev}", headers=s.owner))
    assert (out["fwVersion"], out["board"]) == ("1.1.0", "kincony_kc868_ap")
    st = data(await c.get(f"/api/v1/devices/{dev}/status", headers=s.owner))
    assert st["configRev"] == 1
    assert [x["id"] for x in st["config"]["lights"]] == ["living_main", "desk", "porch"]


async def test_events_and_telemetry_are_stored(app: FastAPI, c: AsyncClient) -> None:
    _s, dev, ing = await online_controller(app, c)
    await ing.handle(f"lumen/v1/d/{dev}/events", b'{"v":1,"type":"input","id":"in1","state":true}')
    await ing.handle(f"lumen/v1/d/{dev}/telemetry", b'{"heap":61000}')
    async with app.state.sessionmaker() as db:
        row = await db.get(DeviceState, uuid.UUID(dev))
    assert row is not None
    assert row.telemetry == {"heap": 61000}


# --- children publishing ------------------------------------------------------------------------------


async def test_children_list_published_on_register_and_remove(app: FastAPI, c: AsyncClient, pub: FakePublisher) -> None:
    s, dev, _ = await online_controller(app, c)
    topic = f"lumen/v1/g/{s.gateway_id}/children"
    published = [p for t, p, r, _ in pub.sent if t == topic and r]
    msg = protocol.Children.model_validate(published[-1])
    assert [(str(ch.device), ch.route_epoch) for ch in msg.children] == [(dev, 1)]
    data(await c.delete(f"/api/v1/devices/{dev}", headers=s.owner))
    msg = protocol.Children.model_validate([p for t, p, r, _ in pub.sent if t == topic][-1])
    assert msg.children == []
    data(await c.delete(f"/api/v1/devices/{s.gateway_id}", headers=s.owner))
    assert pub.sent[-1][:3] == (topic, b"", True)  # retained topic cleared


# --- event hub ----------------------------------------------------------------------------------------


def test_hub_dispatches_by_home_and_marks_slow_clients() -> None:
    hub = EventHub(None)  # type: ignore[arg-type]
    a, b = hub.add({"h1"}), hub.add({"h2", "h1"})
    lone: Subscriber = hub.add({"h3"})
    hub.dispatch({"home": "h1", "v": 1, "event": "device.state", "data": {}})
    assert (a.queue.qsize(), b.queue.qsize(), lone.queue.qsize()) == (1, 1, 0)
    for _ in range(200):
        hub.dispatch({"home": "h3", "v": 1, "event": "x", "data": {}})
    assert lone.overflowed
    assert hub.dropped >= 1
    hub.remove(lone)
    assert lone not in hub.subscribers


async def test_published_event_reaches_hub(app: FastAPI, c: AsyncClient) -> None:
    from app.services.events import publish_event

    s, dev, _ = await online_controller(app, c)
    sub = app.state.hub.add({s.home})
    await asyncio.sleep(0.2)  # let the hub's subscription settle
    await publish_event(app.state.redis, uuid.UUID(s.home), "device.state", {"deviceId": dev})
    msg = await asyncio.wait_for(sub.queue.get(), 3)
    assert (msg["event"], msg["data"]["deviceId"], "home" in msg) == ("device.state", dev, False)


async def test_result_arriving_during_publish_is_never_overwritten(
    app: FastAPI, c: AsyncClient, pub: FakePublisher
) -> None:
    """A fast device can answer before the API's publish call returns; that result must stand."""
    s, dev, ing = await online_controller(app, c)
    original = pub.publish

    async def answering_publish(topic: str, payload: Any, **kw: Any) -> None:
        await original(topic, payload, **kw)
        if topic.endswith("/cmd"):
            await result(ing, dev, payload["id"], "applied")

    pub.publish = answering_publish  # type: ignore[method-assign]
    out = data(await send(c, s, dev, LIGHT_ON), 202)
    assert data(await c.get(f"/api/v1/commands/{out['id']}", headers=s.owner))["status"] == "applied"
