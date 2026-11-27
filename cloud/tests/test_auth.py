"""Accounts: registration, sign-in, token lifecycle, password flows, limits."""

from __future__ import annotations

import asyncio
import base64
import json
from collections.abc import AsyncIterator
from datetime import UTC, datetime, timedelta

import jwt
import pytest
from fastapi import FastAPI
from httpx import AsyncClient
from sqlalchemy import select, update

from app.core.security import ISSUER
from app.core.settings import DEV_SECRET
from app.main import create_public_app
from app.models.account import RefreshToken, Session, User, UserStatus
from app.services.email import DevEmailSender
from tests.conftest import make_settings
from tests.helpers import PASSWORD, bearer, data, error, login, register

pytestmark = pytest.mark.usefixtures("migrated")


@pytest.fixture
def outbox() -> DevEmailSender:
    return DevEmailSender()


@pytest.fixture
def app(outbox: DevEmailSender) -> FastAPI:
    return create_public_app(make_settings(), email=outbox)


@pytest.fixture
async def c(app: FastAPI, make_client) -> AsyncIterator[AsyncClient]:  # type: ignore[no-untyped-def]
    async for client in make_client(app):
        yield client


async def db_exec(app: FastAPI, stmt) -> None:  # type: ignore[no-untyped-def]
    async with app.state.sessionmaker() as s:
        await s.execute(stmt)
        await s.commit()


# --- registration -------------------------------------------------------------


async def test_register_returns_tokens_and_sends_verification(c: AsyncClient, outbox: DevEmailSender) -> None:
    tokens = await register(c, "Alice@Example.com ")
    assert tokens["user"]["email"] == "alice@example.com"
    assert tokens["user"]["emailVerified"] is False
    assert tokens["refreshToken"].startswith("lrt_")
    assert [m.kind for m in outbox.outbox] == ["verify_email"]
    me = data(await c.get("/api/v1/auth/me", headers=bearer(tokens)))
    assert me["email"] == "alice@example.com"


async def test_register_duplicate_email_case_insensitive(c: AsyncClient) -> None:
    await register(c, "bob@example.com")
    r = await c.post("/api/v1/auth/register", json={"email": "BOB@example.com", "password": PASSWORD})
    error(r, 409, "EMAIL_IN_USE")


@pytest.mark.parametrize(
    ("email", "password", "status", "code"),
    [
        ("not-an-email", PASSWORD, 400, "INVALID_REQUEST"),
        ("a@b", PASSWORD, 400, "INVALID_REQUEST"),
        ("carol@example.com", "short", 400, "INVALID_REQUEST"),
        ("carol@example.com", "x" * 129, 400, "INVALID_REQUEST"),
        ("carol@example.com", "carol@example.com", 400, "WEAK_PASSWORD"),
        ("carolina@example.com", "carolina1234", 400, "WEAK_PASSWORD"),
        ("carol@example.com", "aaaaaaaaaaaa", 400, "WEAK_PASSWORD"),
    ],
)
async def test_register_rejects_bad_input(c: AsyncClient, email: str, password: str, status: int, code: str) -> None:
    error(await c.post("/api/v1/auth/register", json={"email": email, "password": password}), status, code)


async def test_register_rejects_unknown_fields(c: AsyncClient) -> None:
    r = await c.post("/api/v1/auth/register", json={"email": "d@example.com", "password": PASSWORD, "role": "admin"})
    error(r, 400, "INVALID_REQUEST")


async def test_password_hash_is_argon2id(c: AsyncClient, app: FastAPI) -> None:
    await register(c, "hash@example.com")
    async with app.state.sessionmaker() as s:
        user = (await s.execute(select(User))).scalar_one()
    assert user.password_hash.startswith("$argon2id$v=19$m=65536,t=3,p=4$")
    assert PASSWORD not in user.password_hash


# --- sign-in -------------------------------------------------------------------


async def test_login_and_wrong_password_and_unknown_user_look_the_same(c: AsyncClient) -> None:
    await register(c, "eve@example.com")
    assert (await login(c, "EVE@example.com"))["user"]["email"] == "eve@example.com"
    wrong = error(
        await c.post("/api/v1/auth/login", json={"email": "eve@example.com", "password": "nope"}),
        401,
        "INVALID_CREDENTIALS",
    )
    unknown = error(
        await c.post("/api/v1/auth/login", json={"email": "nobody@example.com", "password": "nope"}),
        401,
        "INVALID_CREDENTIALS",
    )
    assert wrong == unknown


async def test_disabled_account_cannot_sign_in_or_use_tokens(c: AsyncClient, app: FastAPI) -> None:
    tokens = await register(c, "dis@example.com")
    await db_exec(app, update(User).values(status=UserStatus.disabled))
    error(await c.get("/api/v1/auth/me", headers=bearer(tokens)), 401, "UNAUTHORIZED")
    error(
        await c.post("/api/v1/auth/login", json={"email": "dis@example.com", "password": PASSWORD}),
        401,
        "INVALID_CREDENTIALS",
    )


async def test_login_rate_limit_per_ip_and_email(c: AsyncClient) -> None:
    await register(c, "rl@example.com")
    for _ in range(5):
        error(
            await c.post("/api/v1/auth/login", json={"email": "rl@example.com", "password": "bad"}),
            401,
            "INVALID_CREDENTIALS",
        )
    r = await c.post("/api/v1/auth/login", json={"email": "rl@example.com", "password": PASSWORD})
    error(r, 429, "RATE_LIMITED")
    assert 1 <= int(r.headers["retry-after"]) <= 300


async def test_account_lockout_then_unlock(make_client) -> None:  # type: ignore[no-untyped-def]
    app = create_public_app(make_settings(lockout_after=3, lockout_s=1, login_fail_limit=100, login_ip_fail_limit=100))
    async for c in make_client(app):
        await register(c, "lock@example.com")
        for _ in range(3):
            await c.post("/api/v1/auth/login", json={"email": "lock@example.com", "password": "bad"})
        # Locked: even the right password is refused.
        error(
            await c.post("/api/v1/auth/login", json={"email": "lock@example.com", "password": PASSWORD}),
            423,
            "ACCOUNT_LOCKED",
        )
        await asyncio.sleep(1.1)
        await login(c, "lock@example.com")


async def test_registration_rate_limit(make_client) -> None:  # type: ignore[no-untyped-def]
    app = create_public_app(make_settings(register_ip_limit=2))
    async for c in make_client(app):
        await register(c, "r1@example.com")
        await register(c, "r2@example.com")
        error(
            await c.post("/api/v1/auth/register", json={"email": "r3@example.com", "password": PASSWORD}),
            429,
            "RATE_LIMITED",
        )


# --- access tokens ---------------------------------------------------------------


@pytest.mark.parametrize(
    "header",
    [None, "Bearer", "Bearer not.a.jwt", "Basic dXNlcjpwYXNz", "Bearer " + "a" * 3000],
)
async def test_bad_authorization_headers(c: AsyncClient, header: str | None) -> None:
    headers = {"Authorization": header} if header else {}
    r = await c.get("/api/v1/auth/me", headers=headers)
    error(r, 401, "UNAUTHORIZED")
    assert r.headers["www-authenticate"] == "Bearer"


async def test_forged_expired_and_wrong_type_tokens(c: AsyncClient) -> None:
    tokens = await register(c, "jwt@example.com")
    claims = jwt.decode(tokens["accessToken"], options={"verify_signature": False})
    header = jwt.get_unverified_header(tokens["accessToken"])
    forged = jwt.encode(claims, "another-secret-of-sufficient-length-0123", algorithm="HS256", headers=header)
    expired = jwt.encode(
        {**claims, "exp": datetime.now(UTC) - timedelta(seconds=1)}, DEV_SECRET, algorithm="HS256", headers=header
    )
    wrong_type = jwt.encode({**claims, "typ": "refresh"}, DEV_SECRET, algorithm="HS256", headers=header)

    def b64(obj: dict[str, object]) -> str:
        return base64.urlsafe_b64encode(json.dumps(obj).encode()).rstrip(b"=").decode()

    none_alg = b64({"alg": "none", "typ": "JWT", "kid": header["kid"]}) + "." + b64(claims) + "."
    for token in (forged, expired, wrong_type, none_alg):
        error(await c.get("/api/v1/auth/me", headers={"Authorization": f"Bearer {token}"}), 401, "UNAUTHORIZED")
    assert claims["iss"] == ISSUER


async def test_key_rotation_previous_secret_still_verifies(make_client) -> None:  # type: ignore[no-untyped-def]
    old = create_public_app(make_settings(jwt_secret="old-secret-" + "x" * 30))
    async for c in make_client(old):
        tokens = await register(c, "rot@example.com")
    new = create_public_app(
        make_settings(jwt_secret="new-secret-" + "y" * 30, jwt_previous_secret="old-secret-" + "x" * 30)
    )
    transport_app = new
    async with transport_app.router.lifespan_context(transport_app):
        from httpx import ASGITransport

        async with AsyncClient(transport=ASGITransport(app=new), base_url="http://test") as c2:
            data(await c2.get("/api/v1/auth/me", headers=bearer(tokens)))
    dropped = create_public_app(make_settings(jwt_secret="new-secret-" + "y" * 30))
    async with dropped.router.lifespan_context(dropped):
        from httpx import ASGITransport

        async with AsyncClient(transport=ASGITransport(app=dropped), base_url="http://test") as c3:
            error(await c3.get("/api/v1/auth/me", headers=bearer(tokens)), 401, "UNAUTHORIZED")


# --- refresh rotation ------------------------------------------------------------


async def test_refresh_rotates_and_reuse_revokes_the_session(c: AsyncClient) -> None:
    first = await register(c, "rt@example.com")
    second = data(await c.post("/api/v1/auth/refresh", json={"refreshToken": first["refreshToken"]}))
    assert second["refreshToken"] != first["refreshToken"]
    data(await c.get("/api/v1/auth/me", headers=bearer(second)))
    # The old token comes back (stolen copy): the whole session ends.
    error(
        await c.post("/api/v1/auth/refresh", json={"refreshToken": first["refreshToken"]}), 401, "INVALID_REFRESH_TOKEN"
    )
    error(
        await c.post("/api/v1/auth/refresh", json={"refreshToken": second["refreshToken"]}),
        401,
        "INVALID_REFRESH_TOKEN",
    )
    error(await c.get("/api/v1/auth/me", headers=bearer(second)), 401, "UNAUTHORIZED")


async def test_refresh_other_sessions_unaffected_by_reuse(c: AsyncClient) -> None:
    a = await register(c, "two@example.com")
    b = await login(c, "two@example.com")
    data(await c.post("/api/v1/auth/refresh", json={"refreshToken": a["refreshToken"]}))
    await c.post("/api/v1/auth/refresh", json={"refreshToken": a["refreshToken"]})  # reuse -> revoke session A
    data(await c.get("/api/v1/auth/me", headers=bearer(b)))
    data(await c.post("/api/v1/auth/refresh", json={"refreshToken": b["refreshToken"]}))


async def test_expired_and_unknown_refresh_tokens(c: AsyncClient, app: FastAPI) -> None:
    tokens = await register(c, "exp@example.com")
    error(await c.post("/api/v1/auth/refresh", json={"refreshToken": "lrt_" + "A" * 43}), 401, "INVALID_REFRESH_TOKEN")
    error(await c.post("/api/v1/auth/refresh", json={"refreshToken": "bad"}), 400, "INVALID_REQUEST")
    await db_exec(app, update(RefreshToken).values(expires_at=datetime.now(UTC) - timedelta(seconds=1)))
    error(
        await c.post("/api/v1/auth/refresh", json={"refreshToken": tokens["refreshToken"]}),
        401,
        "INVALID_REFRESH_TOKEN",
    )


async def test_refresh_token_stored_only_as_hash(c: AsyncClient, app: FastAPI) -> None:
    tokens = await register(c, "hashrt@example.com")
    async with app.state.sessionmaker() as s:
        stored = (await s.execute(select(RefreshToken.token_hash))).scalar_one()
    assert tokens["refreshToken"].encode() not in stored
    assert len(stored) == 32


# --- sign-out and sessions ----------------------------------------------------------


async def test_logout_ends_access_and_refresh_at_once(c: AsyncClient) -> None:
    tokens = await register(c, "out@example.com")
    data(await c.post("/api/v1/auth/logout", headers=bearer(tokens)))
    error(await c.get("/api/v1/auth/me", headers=bearer(tokens)), 401, "UNAUTHORIZED")
    error(
        await c.post("/api/v1/auth/refresh", json={"refreshToken": tokens["refreshToken"]}),
        401,
        "INVALID_REFRESH_TOKEN",
    )


async def test_list_and_revoke_sessions(c: AsyncClient) -> None:
    a = await register(c, "sess@example.com")
    b = await login(c, "sess@example.com")
    sessions = data(await c.get("/api/v1/auth/sessions", headers=bearer(a)))
    assert len(sessions) == 2
    assert sum(s["current"] for s in sessions) == 1
    other = next(s for s in sessions if not s["current"])
    data(await c.delete(f"/api/v1/auth/sessions/{other['id']}", headers=bearer(a)))
    error(await c.get("/api/v1/auth/me", headers=bearer(b)), 401, "UNAUTHORIZED")
    error(await c.delete(f"/api/v1/auth/sessions/{other['id']}", headers=bearer(a)), 404, "NOT_FOUND")


async def test_cannot_revoke_someone_elses_session(c: AsyncClient, app: FastAPI) -> None:
    a = await register(c, "mine@example.com")
    await register(c, "theirs@example.com")
    async with app.state.sessionmaker() as s:
        theirs = (
            await s.execute(
                select(Session.id).join(User, User.id == Session.user_id).where(User.email == "theirs@example.com")
            )
        ).scalar_one()
    error(await c.delete(f"/api/v1/auth/sessions/{theirs}", headers=bearer(a)), 404, "NOT_FOUND")


# --- password change and reset ---------------------------------------------------------


async def test_change_password_signs_out_other_sessions(c: AsyncClient) -> None:
    a = await register(c, "pw@example.com")
    b = await login(c, "pw@example.com")
    body = {"currentPassword": "wrong-one", "newPassword": "another good phrase"}
    error(await c.put("/api/v1/auth/password", json=body, headers=bearer(a)), 400, "CURRENT_PASSWORD_WRONG")
    body["currentPassword"] = PASSWORD
    data(await c.put("/api/v1/auth/password", json=body, headers=bearer(a)))
    data(await c.get("/api/v1/auth/me", headers=bearer(a)))
    error(await c.get("/api/v1/auth/me", headers=bearer(b)), 401, "UNAUTHORIZED")
    error(
        await c.post("/api/v1/auth/login", json={"email": "pw@example.com", "password": PASSWORD}),
        401,
        "INVALID_CREDENTIALS",
    )
    await login(c, "pw@example.com", "another good phrase")


async def test_password_reset_flow(c: AsyncClient, outbox: DevEmailSender) -> None:
    tokens = await register(c, "reset@example.com")
    outbox.outbox.clear()
    data(await c.post("/api/v1/auth/password-reset/request", json={"email": "reset@example.com"}), 202)
    data(await c.post("/api/v1/auth/password-reset/request", json={"email": "ghost@example.com"}), 202)
    assert [(m.kind, m.to) for m in outbox.outbox] == [("reset_password", "reset@example.com")]
    token = outbox.outbox[-1].token
    assert token.startswith("lpr_")
    body = {"token": token, "newPassword": "brand new phrase 42"}
    data(await c.post("/api/v1/auth/password-reset/confirm", json=body))
    error(await c.post("/api/v1/auth/password-reset/confirm", json=body), 400, "INVALID_TOKEN")  # single use
    error(await c.get("/api/v1/auth/me", headers=bearer(tokens)), 401, "UNAUTHORIZED")  # all sessions ended
    me = (await login(c, "reset@example.com", "brand new phrase 42"))["user"]
    assert me["emailVerified"] is True


async def test_newer_reset_token_invalidates_older(c: AsyncClient, outbox: DevEmailSender) -> None:
    await register(c, "twice@example.com")
    await c.post("/api/v1/auth/password-reset/request", json={"email": "twice@example.com"})
    first = outbox.outbox[-1].token
    await c.post("/api/v1/auth/password-reset/request", json={"email": "twice@example.com"})
    body = {"token": first, "newPassword": "brand new phrase 42"}
    error(await c.post("/api/v1/auth/password-reset/confirm", json=body), 400, "INVALID_TOKEN")


async def test_reset_request_is_rate_limited_silently(c: AsyncClient, outbox: DevEmailSender) -> None:
    await register(c, "spam@example.com")
    outbox.outbox.clear()
    for _ in range(6):
        data(await c.post("/api/v1/auth/password-reset/request", json={"email": "spam@example.com"}), 202)
    assert len(outbox.outbox) == 3


# --- e-mail verification -----------------------------------------------------------


async def test_verify_email(c: AsyncClient, outbox: DevEmailSender) -> None:
    tokens = await register(c, "ver@example.com")
    token = outbox.outbox[-1].token
    assert token.startswith("lev_")
    data(await c.post("/api/v1/auth/verify-email", json={"token": token}))
    assert data(await c.get("/api/v1/auth/me", headers=bearer(tokens)))["emailVerified"] is True
    error(await c.post("/api/v1/auth/verify-email", json={"token": token}), 400, "INVALID_TOKEN")


async def test_verification_token_cannot_reset_password(c: AsyncClient, outbox: DevEmailSender) -> None:
    await register(c, "mix@example.com")
    token = outbox.outbox[-1].token
    body = {"token": token, "newPassword": "brand new phrase 42"}
    error(await c.post("/api/v1/auth/password-reset/confirm", json=body), 400, "INVALID_TOKEN")


async def test_resend_verification(c: AsyncClient, outbox: DevEmailSender) -> None:
    tokens = await register(c, "again@example.com")
    data(await c.post("/api/v1/auth/verify-email/resend", headers=bearer(tokens)), 202)
    assert len(outbox.outbox) == 2
    error(await c.post("/api/v1/auth/verify-email", json={"token": outbox.outbox[0].token}), 400, "INVALID_TOKEN")
    data(await c.post("/api/v1/auth/verify-email", json={"token": outbox.outbox[1].token}))


async def test_tokens_never_logged(c: AsyncClient, outbox: DevEmailSender, capsys: pytest.CaptureFixture[str]) -> None:
    tokens = await register(c, "quiet@example.com")
    await c.post("/api/v1/auth/password-reset/request", json={"email": "quiet@example.com"})
    out = capsys.readouterr().out
    for secret in (PASSWORD, tokens["refreshToken"], tokens["accessToken"], *(m.token for m in outbox.outbox)):
        assert secret not in out
