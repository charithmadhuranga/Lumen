"""Homes, members, rooms: role matrix over every route, tenant isolation, member rules."""

from __future__ import annotations

import uuid
from collections.abc import AsyncIterator
from dataclasses import dataclass

import pytest
from fastapi import FastAPI
from httpx import AsyncClient
from sqlalchemy import select

from app.core.permissions import matrix
from app.core.security import hash_password, issue_access_token
from app.main import create_public_app
from app.models.account import Session, User
from app.models.home import Home, HomeMember, Role
from tests.conftest import make_settings
from tests.helpers import data, error

pytestmark = pytest.mark.usefixtures("migrated")

_HASH = hash_password("not used by these tests")


@pytest.fixture
def app() -> FastAPI:
    return create_public_app(make_settings())


@pytest.fixture
async def c(app: FastAPI, make_client) -> AsyncIterator[AsyncClient]:  # type: ignore[no-untyped-def]
    async for client in make_client(app):
        yield client


async def user(app: FastAPI, email: str) -> tuple[uuid.UUID, dict[str, str]]:
    """Create a user with a live session; return (id, auth headers). No argon2 per call."""
    async with app.state.sessionmaker() as s:
        u = User(email=email, password_hash=_HASH)
        s.add(u)
        await s.flush()
        sess = Session(user_id=u.id)
        s.add(sess)
        await s.commit()
        token, _ = issue_access_token(app.state.settings, u.id, sess.id)
        return u.id, {"Authorization": f"Bearer {token}"}


async def add_member(app: FastAPI, home_id: str, user_id: uuid.UUID, role: Role) -> None:
    async with app.state.sessionmaker() as s:
        s.add(HomeMember(home_id=uuid.UUID(home_id), user_id=user_id, role=role))
        await s.commit()


@dataclass
class World:
    home: str
    room: str
    headers: dict[str, dict[str, str]]  # role name (or "outsider") -> headers
    ids: dict[str, uuid.UUID]


async def world(app: FastAPI, c: AsyncClient) -> World:
    ids, headers = {}, {}
    for name in ("owner", "admin", "member", "guest", "target", "outsider"):
        ids[name], headers[name] = await user(app, f"{name}@example.com")
    home = data(
        await c.post("/api/v1/homes", json={"name": "Villa", "timezone": "Asia/Colombo"}, headers=headers["owner"]), 201
    )
    for name, role in (("admin", Role.admin), ("member", Role.member), ("guest", Role.guest), ("target", Role.guest)):
        await add_member(app, home["id"], ids[name], role)
    room = data(
        await c.post(f"/api/v1/homes/{home['id']}/rooms", json={"name": "Living"}, headers=headers["owner"]), 201
    )
    # The outsider has a home of its own, so "not a member" is not "no homes at all".
    await c.post("/api/v1/homes", json={"name": "Other", "timezone": "UTC"}, headers=headers["outsider"])
    return World(home["id"], room["id"], headers, ids)


# (method, path template, json body, {role: expected status}); outsider always 404.
ROUTES: list[tuple[str, str, dict[str, object] | None, dict[str, int]]] = [
    ("GET", "/homes/{home}", None, {"owner": 200, "admin": 200, "member": 200, "guest": 200}),
    ("PATCH", "/homes/{home}", {"name": "New"}, {"owner": 200, "admin": 200, "member": 403, "guest": 403}),
    ("DELETE", "/homes/{home}", None, {"owner": 200, "admin": 403, "member": 403, "guest": 403}),
    ("GET", "/homes/{home}/members", None, {"owner": 200, "admin": 200, "member": 200, "guest": 403}),
    (
        "PATCH",
        "/homes/{home}/members/{target}",
        {"role": "member"},
        {"owner": 200, "admin": 200, "member": 403, "guest": 403},
    ),
    ("DELETE", "/homes/{home}/members/{target}", None, {"owner": 200, "admin": 200, "member": 403, "guest": 403}),
    (
        "POST",
        "/homes/{home}/transfer",
        {"userId": "{target}"},
        {"owner": 200, "admin": 403, "member": 403, "guest": 403},
    ),
    ("GET", "/homes/{home}/rooms", None, {"owner": 200, "admin": 200, "member": 200, "guest": 200}),
    ("POST", "/homes/{home}/rooms", {"name": "Kitchen"}, {"owner": 201, "admin": 201, "member": 403, "guest": 403}),
    ("GET", "/rooms/{room}", None, {"owner": 200, "admin": 200, "member": 200, "guest": 200}),
    ("PATCH", "/rooms/{room}", {"name": "Lounge"}, {"owner": 200, "admin": 200, "member": 403, "guest": 403}),
    ("DELETE", "/rooms/{room}", None, {"owner": 200, "admin": 200, "member": 403, "guest": 403}),
]
CASES = [
    pytest.param(m, p, b, role, expected.get(role, 404), id=f"{m} {p} as {role}")
    for m, p, b, expected in ROUTES
    for role in ("owner", "admin", "member", "guest", "outsider")
]


def fill(text: str, w: World) -> str:
    return text.replace("{home}", w.home).replace("{room}", w.room).replace("{target}", str(w.ids["target"]))


@pytest.mark.parametrize(("method", "path", "body", "role", "status"), CASES)
async def test_role_matrix_and_tenant_isolation(
    app: FastAPI, c: AsyncClient, method: str, path: str, body: dict[str, object] | None, role: str, status: int
) -> None:
    w = await world(app, c)
    json_body = {k: fill(v, w) if isinstance(v, str) else v for k, v in body.items()} if body else None
    r = await c.request(method, "/api/v1" + fill(path, w), json=json_body, headers=w.headers[role])
    assert r.status_code == status, f"{method} {path} as {role}: {r.text}"
    if status == 404:
        # Not even "forbidden": the other home's resource must look nonexistent.
        assert r.json()["error"]["code"] == "NOT_FOUND"


def test_permission_matrix_is_the_documented_one() -> None:
    m = matrix()
    assert set(m["owner"]) - set(m["admin"]) == {"home.delete", "home.transfer"}
    assert m["member"] == ["control", "home.read", "members.read", "rooms.read"]
    assert m["guest"] == ["control", "home.read", "rooms.read"]


async def test_unknown_ids_are_404(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    h = w.headers["owner"]
    for path in (f"/homes/{uuid.uuid4()}", f"/rooms/{uuid.uuid4()}", f"/homes/{w.home}/members/{uuid.uuid4()}"):
        method = "DELETE" if "members" in path else "GET"
        error(await c.request(method, "/api/v1" + path, headers=h), 404, "NOT_FOUND")
    error(await c.get("/api/v1/homes/not-a-uuid", headers=h), 400, "INVALID_REQUEST")


async def test_unauthenticated_is_401(c: AsyncClient) -> None:
    error(await c.get("/api/v1/homes"), 401, "UNAUTHORIZED")


# --- homes ----------------------------------------------------------------------------


async def test_create_list_update_delete_home(app: FastAPI, c: AsyncClient) -> None:
    _, h = await user(app, "solo@example.com")
    home = data(
        await c.post("/api/v1/homes", json={"name": "  Beach house ", "timezone": "Europe/Berlin"}, headers=h), 201
    )
    assert home["name"] == "Beach house"
    assert home["role"] == "owner"
    assert [x["id"] for x in data(await c.get("/api/v1/homes", headers=h))] == [home["id"]]
    updated = data(await c.patch(f"/api/v1/homes/{home['id']}", json={"timezone": "Asia/Colombo"}, headers=h))
    assert updated["timezone"] == "Asia/Colombo"
    assert updated["name"] == "Beach house"
    data(await c.delete(f"/api/v1/homes/{home['id']}", headers=h))
    assert data(await c.get("/api/v1/homes", headers=h)) == []


@pytest.mark.parametrize(
    "body",
    [
        {"name": "x", "timezone": "Mars/Olympus"},
        {"name": "", "timezone": "UTC"},
        {"name": "x" * 65, "timezone": "UTC"},
        {"name": "x"},
        {"name": "x", "timezone": "UTC", "ownerId": str(uuid.uuid4())},
    ],
)
async def test_home_validation(app: FastAPI, c: AsyncClient, body: dict[str, str]) -> None:
    _, h = await user(app, "v@example.com")
    error(await c.post("/api/v1/homes", json=body, headers=h), 400, "INVALID_REQUEST")


async def test_home_list_shows_only_own_homes_with_roles(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    homes = data(await c.get("/api/v1/homes", headers=w.headers["guest"]))
    assert [(x["id"], x["role"]) for x in homes] == [(w.home, "guest")]


async def test_home_capacity(app: FastAPI, c: AsyncClient) -> None:
    _, h = await user(app, "many@example.com")
    for i in range(20):
        await c.post("/api/v1/homes", json={"name": f"H{i}", "timezone": "UTC"}, headers=h)
    error(await c.post("/api/v1/homes", json={"name": "H21", "timezone": "UTC"}, headers=h), 409, "CAPACITY")


# --- members --------------------------------------------------------------------------


async def test_member_list(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    members = data(await c.get(f"/api/v1/homes/{w.home}/members", headers=w.headers["member"]))
    assert {(m["email"], m["role"]) for m in members} == {
        ("owner@example.com", "owner"),
        ("admin@example.com", "admin"),
        ("member@example.com", "member"),
        ("guest@example.com", "guest"),
        ("target@example.com", "guest"),
    }


async def test_role_change_rules(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    base = f"/api/v1/homes/{w.home}/members"
    admin, owner = w.headers["admin"], w.headers["owner"]
    error(await c.patch(f"{base}/{w.ids['admin']}", json={"role": "member"}, headers=admin), 400, "INVALID_REQUEST")
    error(await c.patch(f"{base}/{w.ids['owner']}", json={"role": "member"}, headers=admin), 403, "FORBIDDEN")
    error(await c.patch(f"{base}/{w.ids['target']}", json={"role": "admin"}, headers=admin), 403, "FORBIDDEN")
    error(await c.patch(f"{base}/{w.ids['target']}", json={"role": "owner"}, headers=owner), 400, "INVALID_REQUEST")
    data(await c.patch(f"{base}/{w.ids['target']}", json={"role": "admin"}, headers=owner))
    # Now an admin: another admin cannot demote or remove them.
    error(await c.patch(f"{base}/{w.ids['target']}", json={"role": "guest"}, headers=admin), 403, "FORBIDDEN")
    error(await c.delete(f"{base}/{w.ids['target']}", headers=admin), 403, "FORBIDDEN")
    error(await c.patch(f"{base}/{w.ids['target']}", json={"role": "boss"}, headers=owner), 400, "INVALID_REQUEST")


async def test_leave_and_owner_cannot_leave(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    base = f"/api/v1/homes/{w.home}/members"
    data(await c.delete(f"{base}/{w.ids['guest']}", headers=w.headers["guest"]))
    error(await c.get(f"/api/v1/homes/{w.home}", headers=w.headers["guest"]), 404, "NOT_FOUND")
    error(await c.delete(f"{base}/{w.ids['owner']}", headers=w.headers["owner"]), 409, "OWNER_MUST_TRANSFER")
    error(await c.delete(f"{base}/{w.ids['owner']}", headers=w.headers["admin"]), 403, "FORBIDDEN")


async def test_transfer_ownership(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    owner = w.headers["owner"]
    error(
        await c.post(f"/api/v1/homes/{w.home}/transfer", json={"userId": str(w.ids["owner"])}, headers=owner),
        400,
        "INVALID_REQUEST",
    )
    error(
        await c.post(f"/api/v1/homes/{w.home}/transfer", json={"userId": str(w.ids["outsider"])}, headers=owner),
        404,
        "NOT_FOUND",
    )
    data(await c.post(f"/api/v1/homes/{w.home}/transfer", json={"userId": str(w.ids["member"])}, headers=owner))
    assert data(await c.get(f"/api/v1/homes/{w.home}", headers=owner))["role"] == "admin"
    assert data(await c.get(f"/api/v1/homes/{w.home}", headers=w.headers["member"]))["role"] == "owner"
    async with app.state.sessionmaker() as s:
        home = (await s.execute(select(Home))).scalars().first()
        owners = (await s.execute(select(HomeMember).where(HomeMember.role == Role.owner))).scalars().all()
    assert home is not None
    assert {o.user_id for o in owners if str(o.home_id) == w.home} == {w.ids["member"]}
    # The previous owner (now admin) can leave; the new owner cannot.
    data(await c.delete(f"/api/v1/homes/{w.home}/members/{w.ids['owner']}", headers=owner))


# --- rooms ----------------------------------------------------------------------------


async def test_rooms_crud_ordering_and_conflicts(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    h, base = w.headers["admin"], f"/api/v1/homes/{w.home}/rooms"
    data(await c.post(base, json={"name": "Kitchen", "sortOrder": 2}, headers=h), 201)
    data(await c.post(base, json={"name": "Bedroom", "sortOrder": 1}, headers=h), 201)
    error(await c.post(base, json={"name": "Kitchen"}, headers=h), 409, "CONFLICT")
    names = [r["name"] for r in data(await c.get(base, headers=h))]
    assert names == ["Living", "Bedroom", "Kitchen"]
    error(await c.patch(f"/api/v1/rooms/{w.room}", json={"name": "Kitchen"}, headers=h), 409, "CONFLICT")
    error(await c.patch(f"/api/v1/rooms/{w.room}", json={"sortOrder": 10001}, headers=h), 400, "INVALID_REQUEST")
    renamed = data(await c.patch(f"/api/v1/rooms/{w.room}", json={"name": "Lounge", "sortOrder": 5}, headers=h))
    assert (renamed["name"], renamed["sortOrder"], renamed["homeId"]) == ("Lounge", 5, w.home)


async def test_room_capacity(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    base = f"/api/v1/homes/{w.home}/rooms"
    for i in range(99):  # "Living" already exists
        await c.post(base, json={"name": f"R{i}"}, headers=w.headers["owner"])
    error(await c.post(base, json={"name": "One too many"}, headers=w.headers["owner"]), 409, "CAPACITY")


async def test_deleting_home_removes_rooms_and_memberships(app: FastAPI, c: AsyncClient) -> None:
    w = await world(app, c)
    data(await c.delete(f"/api/v1/homes/{w.home}", headers=w.headers["owner"]))
    error(await c.get(f"/api/v1/rooms/{w.room}", headers=w.headers["owner"]), 404, "NOT_FOUND")
    assert data(await c.get("/api/v1/homes", headers=w.headers["admin"])) == []
