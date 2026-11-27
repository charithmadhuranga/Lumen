from __future__ import annotations

from httpx import AsyncClient

from app.main import create_public_app
from tests.conftest import make_settings


async def test_healthz(client: AsyncClient) -> None:
    r = await client.get("/healthz")
    assert r.status_code == 200
    assert r.json() == {"success": True, "data": {"status": "ok"}, "error": None}


async def test_readyz_ok_when_database_and_redis_answer(client: AsyncClient) -> None:
    r = await client.get("/readyz")
    assert r.status_code == 200, r.text
    assert r.json()["data"] == {"checks": {"database": "ok", "redis": "ok"}}


async def test_readyz_503_names_the_failing_dependency_without_leaking_urls(make_client, migrated) -> None:  # type: ignore[no-untyped-def]
    app = create_public_app(make_settings(redis_url="redis://:hunter2@127.0.0.1:1/0", readiness_timeout_s=0.5))
    async for c in make_client(app):
        r = await c.get("/readyz")
    assert r.status_code == 503
    body = r.json()
    assert body["success"] is False
    assert body["error"]["code"] == "UNAVAILABLE"
    assert body["data"]["checks"] == {"database": "ok", "redis": "unavailable"}
    assert "hunter2" not in r.text
    assert "127.0.0.1" not in r.text


async def test_readyz_503_when_database_is_down(make_client) -> None:  # type: ignore[no-untyped-def]
    app = create_public_app(
        make_settings(database_url="postgresql+asyncpg://lumen:secretpw@127.0.0.1:1/x", readiness_timeout_s=0.5)
    )
    async for c in make_client(app):
        r = await c.get("/readyz")
    assert r.status_code == 503
    assert r.json()["data"]["checks"]["database"] == "unavailable"
    assert "secretpw" not in r.text
