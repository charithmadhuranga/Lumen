"""Claims, gateway and controller registration, device management, local
tickets and the broker hooks (in-process, test database)."""

from __future__ import annotations

import base64
import hashlib
import time
import uuid
from collections.abc import AsyncIterator
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import Any

import pytest
from fastapi import FastAPI
from httpx import ASGITransport, AsyncClient
from sqlalchemy import select, update

from app.core.settings import DEV_SECRET
from app.main import create_internal_app, create_public_app
from app.models.device import Claim, Device
from app.models.home import Role
from app.services import tickets
from tests.conftest import make_settings
from tests.helpers import data, error
from tests.test_homes import add_member, user

pytestmark = pytest.mark.usefixtures("migrated")

SETUP_CODE = "K7QM-2XRA"
HW_GW = {"model": "raspberry-pi-5", "mac": "d8:3a:dd:00:00:01", "chip": "bcm2712"}
HW_CTRL = {"model": "kc868-ap", "board": "kincony_kc868_ap", "mac": "a0:b7:65:12:07:98", "chip": "esp32"}


def proof_hash(code: str) -> str:
    return hashlib.sha256(code.replace("-", "").upper().encode()).hexdigest()


def basic(gateway_id: str, secret: str) -> dict[str, str]:
    return {"Authorization": "Basic " + base64.b64encode(f"g:{gateway_id}:{secret}".encode()).decode()}


@pytest.fixture
def app() -> FastAPI:
    # Kicking is exercised by the live broker tests; here there is no EMQX API.
    return create_public_app(make_settings(emqx_api_url=""))


@pytest.fixture
async def c(app: FastAPI, make_client) -> AsyncIterator[AsyncClient]:  # type: ignore[no-untyped-def]
    async for client in make_client(app):
        yield client


@dataclass
class Setup:
    home: str
    owner: dict[str, str]
    gateway_id: str
    secret: str


async def gateway_claim(c: AsyncClient, home: str, headers: dict[str, str], code: str = SETUP_CODE) -> str:
    r = await c.post(f"/api/v1/homes/{home}/gateway-claims", json={"setupProofHash": proof_hash(code)}, headers=headers)
    return data(r, 201)["claimToken"]  # type: ignore[no-any-return]


async def register_gateway(c: AsyncClient, token: str, code: str = SETUP_CODE, hw: dict[str, str] | None = None) -> Any:
    body = {"v": 1, "claimToken": token, "setupProof": code, "hw": hw or HW_GW, "version": "1.0.0"}
    return await c.post("/api/v1/gateway/register", json=body)


async def setup(app: FastAPI, c: AsyncClient) -> Setup:
    _, owner = await user(app, f"owner-{uuid.uuid4().hex[:6]}@example.com")
    home = data(await c.post("/api/v1/homes", json={"name": "H", "timezone": "UTC"}, headers=owner), 201)["id"]
    reg = data(await register_gateway(c, await gateway_claim(c, home, owner)), 201)
    return Setup(home, owner, reg["gatewayId"], reg["mqtt"]["password"])


async def controller_claim(c: AsyncClient, s: Setup) -> str:
    r = await c.post(f"/api/v1/homes/{s.home}/claims", json={"gatewayId": s.gateway_id}, headers=s.owner)
    return data(r, 201)["claimToken"]  # type: ignore[no-any-return]


async def register_child(c: AsyncClient, s: Setup, token: str, hw: dict[str, str] | None = None) -> Any:
    body = {
        "v": 1,
        "claimToken": token,
        "hw": hw or HW_CTRL,
        "fw": "1.1.0",
        "controllerId": "lumen-0798",
        "url": "http://192.168.1.42",
    }
    return await c.post(
        f"/api/v1/gateways/{s.gateway_id}/children/register", json=body, headers=basic(s.gateway_id, s.secret)
    )


# --- gateway claim and registration ------------------------------------------------------


async def test_gateway_registration(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    devices = data(await c.get(f"/api/v1/homes/{s.home}/devices", headers=s.owner))
    assert [(d["id"], d["kind"], d["hwModel"]) for d in devices] == [(s.gateway_id, "gateway", "raspberry-pi-5")]
    async with app.state.sessionmaker() as db:
        stored = (await db.execute(select(Claim))).scalar_one()
    assert stored.used_at is not None
    assert str(stored.device_id) == s.gateway_id


async def test_gateway_register_response_matches_protocol(app: FastAPI, c: AsyncClient) -> None:
    _, owner = await user(app, "p@example.com")
    home = data(await c.post("/api/v1/homes", json={"name": "H", "timezone": "UTC"}, headers=owner), 201)["id"]
    reg = data(await register_gateway(c, await gateway_claim(c, home, owner)), 201)
    assert set(reg) == {"v", "gatewayId", "mqtt"}
    assert reg["mqtt"]["username"] == f"g:{reg['gatewayId']}"
    assert reg["mqtt"]["url"].startswith("mqtts://")
    assert len(reg["mqtt"]["password"]) == 43


async def test_claim_is_single_use_and_expires(app: FastAPI, c: AsyncClient) -> None:
    _, owner = await user(app, "once@example.com")
    home = data(await c.post("/api/v1/homes", json={"name": "H", "timezone": "UTC"}, headers=owner), 201)["id"]
    token = await gateway_claim(c, home, owner)
    data(await register_gateway(c, token), 201)
    error(await register_gateway(c, token), 400, "INVALID_CLAIM")
    expired = await gateway_claim(c, home, owner)
    async with app.state.sessionmaker() as db:
        await db.execute(
            update(Claim).where(Claim.used_at.is_(None)).values(expires_at=datetime.now(UTC) - timedelta(seconds=1))
        )
        await db.commit()
    error(await register_gateway(c, expired), 400, "INVALID_CLAIM")


async def test_wrong_setup_code_burns_the_claim(app: FastAPI, c: AsyncClient) -> None:
    _, owner = await user(app, "code@example.com")
    home = data(await c.post("/api/v1/homes", json={"name": "H", "timezone": "UTC"}, headers=owner), 201)["id"]
    token = await gateway_claim(c, home, owner)
    error(await register_gateway(c, token, code="ZZZZ-ZZZZ"), 400, "INVALID_SETUP_CODE")
    error(await register_gateway(c, token), 400, "INVALID_CLAIM")  # no second guess with the same claim


async def test_setup_code_is_normalised(app: FastAPI, c: AsyncClient) -> None:
    _, owner = await user(app, "norm@example.com")
    home = data(await c.post("/api/v1/homes", json={"name": "H", "timezone": "UTC"}, headers=owner), 201)["id"]
    data(await register_gateway(c, await gateway_claim(c, home, owner), code="k7qm 2xra"), 201)


async def test_reregistering_a_gateway_rotates_its_credential(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    again = data(await register_gateway(c, await gateway_claim(c, s.home, s.owner)), 201)
    assert again["gatewayId"] == s.gateway_id
    assert again["mqtt"]["password"] != s.secret
    error(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, s.secret)), 401, "UNAUTHORIZED")
    data(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, again["mqtt"]["password"])))


async def test_only_owner_and_admin_create_claims(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    for role, status in ((Role.admin, 201), (Role.member, 403), (Role.guest, 403)):
        uid, h = await user(app, f"{role.value}@example.com")
        await add_member(app, s.home, uid, role)
        r = await c.post(
            f"/api/v1/homes/{s.home}/gateway-claims", json={"setupProofHash": proof_hash(SETUP_CODE)}, headers=h
        )
        assert r.status_code == status, (role, r.text)
        r = await c.post(f"/api/v1/homes/{s.home}/claims", json={"gatewayId": s.gateway_id}, headers=h)
        assert r.status_code == status, (role, r.text)
    _, outsider = await user(app, "out@example.com")
    error(
        await c.post(f"/api/v1/homes/{s.home}/claims", json={"gatewayId": s.gateway_id}, headers=outsider),
        404,
        "NOT_FOUND",
    )


async def test_controller_claim_needs_a_gateway_of_the_same_home(app: FastAPI, c: AsyncClient) -> None:
    a = await setup(app, c)
    b = await setup(app, c)
    r = await c.post(f"/api/v1/homes/{a.home}/claims", json={"gatewayId": b.gateway_id}, headers=a.owner)
    error(r, 404, "NOT_FOUND")


# --- gateway authentication ------------------------------------------------------------------


@pytest.mark.parametrize(
    "header",
    [
        None,
        "Bearer x",
        "Basic !!!",
        "Basic " + base64.b64encode(b"nocolon").decode(),
        "Basic " + base64.b64encode(b"g:not-a-uuid:secret").decode(),
        "Basic " + base64.b64encode(f"d:{uuid.uuid4()}:secret".encode()).decode(),
    ],
)
async def test_gateway_auth_rejects_bad_headers(c: AsyncClient, header: str | None) -> None:
    r = await c.get("/api/v1/gateway/children", headers={"Authorization": header} if header else {})
    error(r, 401, "UNAUTHORIZED")


async def test_gateway_auth_wrong_secret_and_rate_limit(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    for _ in range(20):
        error(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, "x" * 43)), 401, "UNAUTHORIZED")
    error(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, s.secret)), 429, "RATE_LIMITED")


# --- controllers -------------------------------------------------------------------------------


async def test_controller_registration_and_recovery_listing(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    reg = data(await register_child(c, s, await controller_claim(c, s)), 201)
    assert set(reg) == {"v", "deviceId", "localKey", "ticketEpoch", "gatewayId"}
    assert reg["gatewayId"] == s.gateway_id
    assert reg["ticketEpoch"] == 0
    children = data(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, s.secret)))
    assert children == [
        {
            "deviceId": reg["deviceId"],
            "controllerId": "lumen-0798",
            "lanUrl": "http://192.168.1.42",
            "routeEpoch": 1,
            "ticketEpoch": 0,
            "localKey": reg["localKey"],
        }
    ]
    listed = data(await c.get(f"/api/v1/gateways/{s.gateway_id}/children", headers=s.owner))
    assert [d["id"] for d in listed] == [reg["deviceId"]]


async def test_local_key_is_encrypted_at_rest(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    reg = data(await register_child(c, s, await controller_claim(c, s)), 201)
    key = base64.urlsafe_b64decode(reg["localKey"] + "=")
    async with app.state.sessionmaker() as db:
        device = await db.get(Device, uuid.UUID(reg["deviceId"]))
    assert device is not None
    assert device.local_key_enc is not None
    assert key not in device.local_key_enc
    assert len(device.local_key_enc) == 12 + 32 + 16


async def test_child_claim_for_another_gateway_is_refused_and_not_consumed(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    other = data(
        await register_gateway(c, await gateway_claim(c, s.home, s.owner), hw={**HW_GW, "mac": "d8:3a:dd:00:00:02"}),
        201,
    )
    token = await controller_claim(c, s)  # issued for s.gateway_id
    impostor = Setup(s.home, s.owner, other["gatewayId"], other["mqtt"]["password"])
    error(await register_child(c, impostor, token), 403, "FORBIDDEN")
    data(await register_child(c, s, token), 201)  # the right gateway can still use it


async def test_gateway_cannot_register_children_for_another_gateway_path(app: FastAPI, c: AsyncClient) -> None:
    a = await setup(app, c)
    b = await setup(app, c)
    body = {
        "v": 1,
        "claimToken": await controller_claim(c, b),
        "hw": HW_CTRL,
        "fw": "1.1.0",
        "controllerId": "lumen-0798",
        "url": "http://192.168.1.42",
    }
    r = await c.post(
        f"/api/v1/gateways/{b.gateway_id}/children/register", json=body, headers=basic(a.gateway_id, a.secret)
    )
    error(r, 403, "FORBIDDEN")


async def test_repairing_a_controller_reuses_it_and_raises_epochs(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    first = data(await register_child(c, s, await controller_claim(c, s)), 201)
    second = data(await register_child(c, s, await controller_claim(c, s)), 201)
    assert second["deviceId"] == first["deviceId"]
    assert second["localKey"] != first["localKey"]
    assert second["ticketEpoch"] == first["ticketEpoch"] + 1
    child = data(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, s.secret)))[0]
    assert child["routeEpoch"] == 2


@pytest.mark.parametrize(
    "patch",
    [
        {"url": "https://192.168.1.42"},
        {"controllerId": "Lumen 0798"},
        {"fw": "latest"},
        {"v": 2},
        {"hw": {**HW_CTRL, "mac": "A0-B7-65-12-07-98"}},
        {"secret": "x"},
    ],
)
async def test_child_registration_validation(app: FastAPI, c: AsyncClient, patch: dict[str, object]) -> None:
    s = await setup(app, c)
    body = {
        "v": 1,
        "claimToken": await controller_claim(c, s),
        "hw": HW_CTRL,
        "fw": "1.1.0",
        "controllerId": "lumen-0798",
        "url": "http://192.168.1.42",
        **patch,
    }
    r = await c.post(
        f"/api/v1/gateways/{s.gateway_id}/children/register", json=body, headers=basic(s.gateway_id, s.secret)
    )
    error(r, 400, "INVALID_REQUEST")


# --- device management ----------------------------------------------------------------------


async def test_rename_assign_room_and_isolation(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    dev = data(await register_child(c, s, await controller_claim(c, s)), 201)["deviceId"]
    room = data(await c.post(f"/api/v1/homes/{s.home}/rooms", json={"name": "Living"}, headers=s.owner), 201)["id"]
    out = data(await c.patch(f"/api/v1/devices/{dev}", json={"name": "Ceiling", "roomId": room}, headers=s.owner))
    assert (out["name"], out["roomId"]) == ("Ceiling", room)
    out = data(await c.patch(f"/api/v1/devices/{dev}", json={"roomId": None}, headers=s.owner))
    assert out["roomId"] is None
    assert out["name"] == "Ceiling"
    other = await setup(app, c)
    foreign_room = data(
        await c.post(f"/api/v1/homes/{other.home}/rooms", json={"name": "X"}, headers=other.owner), 201
    )["id"]
    error(await c.patch(f"/api/v1/devices/{dev}", json={"roomId": foreign_room}, headers=s.owner), 404, "NOT_FOUND")
    for method in ("GET", "PATCH", "DELETE"):
        r = await c.request(
            method, f"/api/v1/devices/{dev}", json={"name": "x"} if method == "PATCH" else None, headers=other.owner
        )
        error(r, 404, "NOT_FOUND")
    error(await c.post(f"/api/v1/devices/{dev}/local-ticket", json={}, headers=other.owner), 404, "NOT_FOUND")


async def test_member_cannot_manage_devices(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    uid, h = await user(app, "m@example.com")
    await add_member(app, s.home, uid, Role.member)
    data(await c.get(f"/api/v1/devices/{s.gateway_id}", headers=h))
    error(await c.patch(f"/api/v1/devices/{s.gateway_id}", json={"name": "x"}, headers=h), 403, "FORBIDDEN")
    error(await c.delete(f"/api/v1/devices/{s.gateway_id}", headers=h), 403, "FORBIDDEN")


async def test_releasing_a_gateway_revokes_it_and_unassigns_children(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    dev = data(await register_child(c, s, await controller_claim(c, s)), 201)["deviceId"]
    data(await c.delete(f"/api/v1/devices/{s.gateway_id}", headers=s.owner))
    error(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, s.secret)), 401, "UNAUTHORIZED")
    child = data(await c.get(f"/api/v1/devices/{dev}", headers=s.owner))
    assert child["gatewayId"] is None


async def test_releasing_a_controller(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    dev = data(await register_child(c, s, await controller_claim(c, s)), 201)["deviceId"]
    data(await c.delete(f"/api/v1/devices/{dev}", headers=s.owner))
    assert data(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, s.secret))) == []


async def test_deleting_the_home_removes_devices_and_credentials(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    data(await c.delete(f"/api/v1/homes/{s.home}", headers=s.owner))
    error(await c.get("/api/v1/gateway/children", headers=basic(s.gateway_id, s.secret)), 401, "UNAUTHORIZED")


# --- local tickets ------------------------------------------------------------------------


async def test_local_tickets_by_role(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    reg = data(await register_child(c, s, await controller_claim(c, s)), 201)
    key = base64.urlsafe_b64decode(reg["localKey"] + "=")
    dev = reg["deviceId"]
    now = int(time.time())
    for role in (Role.admin, Role.member, Role.guest):
        uid, h = await user(app, f"t-{role.value}@example.com")
        await add_member(app, s.home, uid, role)
        out = data(await c.post(f"/api/v1/devices/{dev}/local-ticket", json={}, headers=h))
        claims = tickets.verify(out["ticket"], key, dev, reg["ticketEpoch"], now)
        assert (claims.sub, claims.scp, claims.exp - claims.iat) == (f"u:{uid}", "control", 7 * 86400)
        assert out["lan"] == {"controllerId": "lumen-0798", "url": "http://192.168.1.42"}
        r = await c.post(f"/api/v1/devices/{dev}/local-ticket", json={"scope": "admin"}, headers=h)
        if role == Role.admin:
            admin = tickets.verify(data(r)["ticket"], key, dev, reg["ticketEpoch"], now)
            assert admin.scp == "admin"
            assert admin.exp - admin.iat == 12 * 3600
        else:
            error(r, 403, "FORBIDDEN")
    error(
        await c.post(f"/api/v1/devices/{dev}/local-ticket", json={"scope": "root"}, headers=s.owner),
        400,
        "INVALID_REQUEST",
    )
    error(
        await c.post(f"/api/v1/devices/{s.gateway_id}/local-ticket", json={}, headers=s.owner), 400, "INVALID_REQUEST"
    )


async def test_repairing_invalidates_old_tickets(app: FastAPI, c: AsyncClient) -> None:
    s = await setup(app, c)
    first = data(await register_child(c, s, await controller_claim(c, s)), 201)
    old = data(await c.post(f"/api/v1/devices/{first['deviceId']}/local-ticket", json={}, headers=s.owner))["ticket"]
    second = data(await register_child(c, s, await controller_claim(c, s)), 201)
    key = base64.urlsafe_b64decode(second["localKey"] + "=")
    with pytest.raises(tickets.TicketError):
        tickets.verify(old, key, second["deviceId"], second["ticketEpoch"], int(time.time()))


# --- broker hooks ---------------------------------------------------------------------------


@pytest.fixture
async def hooks(app: FastAPI) -> AsyncIterator[AsyncClient]:
    internal = create_internal_app(app.state.settings)
    async with (
        internal.router.lifespan_context(internal),
        AsyncClient(transport=ASGITransport(app=internal), base_url="http://internal") as client,
    ):
        yield client


SECRET_HEADER = {"X-Lumen-Hook-Secret": DEV_SECRET}


async def test_hook_requires_secret(hooks: AsyncClient) -> None:
    body = {"clientid": "svc:x", "username": "svc:x", "password": DEV_SECRET}
    error(await hooks.post("/internal/mqtt/auth", json=body), 403, "FORBIDDEN")
    error(await hooks.post("/internal/mqtt/auth", json=body, headers={"X-Lumen-Hook-Secret": "nope"}), 403, "FORBIDDEN")
    assert (await hooks.post("/internal/mqtt/auth", json=body, headers=SECRET_HEADER)).json()["result"] == "allow"


async def test_auth_hook(app: FastAPI, c: AsyncClient, hooks: AsyncClient) -> None:
    s = await setup(app, c)
    gid = f"g:{s.gateway_id}"

    async def auth(clientid: str, username: str, password: str) -> str:
        r = await hooks.post(
            "/internal/mqtt/auth",
            json={"clientid": clientid, "username": username, "password": password},
            headers=SECRET_HEADER,
        )
        assert r.status_code == 200
        return r.json()["result"]  # type: ignore[no-any-return]

    assert await auth(gid, gid, s.secret) == "allow"
    assert await auth(gid, gid, "wrong") == "deny"
    assert await auth(gid, gid, "") == "deny"
    assert await auth(gid, f"g:{uuid.uuid4()}", s.secret) == "deny"
    assert await auth(f"d:{s.gateway_id}", f"d:{s.gateway_id}", s.secret) == "deny"
    assert await auth("svc:bridge", "svc:bridge", "wrong") == "deny"
    data(await c.delete(f"/api/v1/devices/{s.gateway_id}", headers=s.owner))
    assert await auth(gid, gid, s.secret) == "deny"


async def test_acl_hook_follows_assignments(app: FastAPI, c: AsyncClient, hooks: AsyncClient) -> None:
    s = await setup(app, c)
    dev = data(await register_child(c, s, await controller_claim(c, s)), 201)["deviceId"]
    gid = f"g:{s.gateway_id}"

    async def acl(topic: str, action: str) -> str:
        r = await hooks.post(
            "/internal/mqtt/acl",
            json={"clientid": gid, "username": gid, "topic": topic, "action": action},
            headers=SECRET_HEADER,
        )
        return r.json()["result"]  # type: ignore[no-any-return]

    assert await acl(f"lumen/v1/d/{dev}/state", "publish") == "allow"
    assert await acl(f"lumen/v1/d/{dev}/cmd", "subscribe") == "allow"
    assert await acl(f"lumen/v1/d/{dev}/cmd", "publish") == "deny"
    assert await acl(f"lumen/v1/d/{uuid.uuid4()}/state", "publish") == "deny"
    assert await acl(f"lumen/v1/g/{s.gateway_id}/children", "subscribe") == "allow"
    assert await acl("lumen/v1/#", "subscribe") == "deny"
    data(await c.delete(f"/api/v1/devices/{dev}", headers=s.owner))
    assert await acl(f"lumen/v1/d/{dev}/state", "publish") == "deny"
