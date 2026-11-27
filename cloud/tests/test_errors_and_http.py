"""Envelope, error mapping, body limit and response headers."""

from __future__ import annotations

from collections.abc import AsyncIterator

import pytest
from fastapi import FastAPI
from httpx import AsyncClient
from pydantic import BaseModel, Field

from app.core.errors import ApiError, ok
from app.main import MAX_BODY_BYTES, create_public_app
from tests.conftest import make_settings


class Thing(BaseModel):
    name: str = Field(min_length=1, max_length=8)
    count: int = Field(ge=0)


def app_with_test_routes() -> FastAPI:
    app = create_public_app(make_settings())

    @app.post("/test/thing")
    async def make_thing(thing: Thing) -> dict[str, object]:
        return ok(thing.model_dump())

    @app.get("/test/conflict")
    async def conflict() -> None:
        raise ApiError(409, "CONFLICT", "already exists")

    @app.get("/test/boom")
    async def boom() -> None:
        raise RuntimeError("database password is hunter2")

    return app


@pytest.fixture
async def c(make_client, migrated) -> AsyncIterator[AsyncClient]:  # type: ignore[no-untyped-def]
    async for client in make_client(app_with_test_routes()):
        yield client


def assert_error(r, status: int, code: str) -> dict[str, object]:  # type: ignore[no-untyped-def]
    assert r.status_code == status, r.text
    body = r.json()
    assert body["success"] is False
    assert body["data"] is None
    assert body["error"]["code"] == code
    assert isinstance(body["error"]["message"], str)
    assert body["error"]["message"]
    return body  # type: ignore[no-any-return]


async def test_success_envelope(c: AsyncClient) -> None:
    r = await c.post("/test/thing", json={"name": "lamp", "count": 2})
    assert r.json() == {"success": True, "data": {"name": "lamp", "count": 2}, "error": None}


async def test_unknown_route_is_404_envelope(c: AsyncClient) -> None:
    assert_error(await c.get("/api/v1/nope"), 404, "NOT_FOUND")


async def test_wrong_method_is_405_envelope(c: AsyncClient) -> None:
    assert_error(await c.delete("/healthz"), 405, "METHOD_NOT_ALLOWED")


async def test_validation_error_is_400_and_does_not_echo_input(c: AsyncClient) -> None:
    body = assert_error(await c.post("/test/thing", json={"name": "x" * 50, "count": 1}), 400, "INVALID_REQUEST")
    assert "name" in body["error"]["message"]  # type: ignore[index]
    assert "x" * 50 not in str(body)


async def test_malformed_json_is_400(c: AsyncClient) -> None:
    r = await c.post("/test/thing", content=b"{nope", headers={"content-type": "application/json"})
    assert_error(r, 400, "INVALID_REQUEST")


async def test_api_error_keeps_code_and_status(c: AsyncClient) -> None:
    body = assert_error(await c.get("/test/conflict"), 409, "CONFLICT")
    assert body["error"]["message"] == "already exists"  # type: ignore[index]


async def test_unexpected_exception_is_500_without_details(c: AsyncClient) -> None:
    r = await c.get("/test/boom")
    assert_error(r, 500, "INTERNAL_ERROR")
    assert "hunter2" not in r.text
    assert "RuntimeError" not in r.text


async def test_body_over_limit_by_content_length_is_413(c: AsyncClient) -> None:
    r = await c.post("/test/thing", content=b"x" * (MAX_BODY_BYTES + 1), headers={"content-type": "application/json"})
    assert_error(r, 413, "PAYLOAD_TOO_LARGE")


async def test_chunked_body_over_limit_is_413(c: AsyncClient) -> None:
    async def chunks() -> AsyncIterator[bytes]:
        for _ in range(MAX_BODY_BYTES // 1024 + 2):
            yield b"x" * 1024

    r = await c.post("/test/thing", content=chunks(), headers={"content-type": "application/json"})
    assert_error(r, 413, "PAYLOAD_TOO_LARGE")


async def test_security_headers_and_request_id(c: AsyncClient) -> None:
    r = await c.get("/healthz", headers={"X-Request-Id": "abc123"})
    assert r.headers["cache-control"] == "no-store"
    assert r.headers["x-content-type-options"] == "nosniff"
    assert r.headers["x-request-id"] == "abc123"
    generated = (await c.get("/healthz")).headers["x-request-id"]
    assert len(generated) == 32


async def test_openapi_is_hidden_in_production(make_client) -> None:  # type: ignore[no-untyped-def]
    strong = "s" * 40
    app = create_public_app(
        make_settings(
            environment="production",
            jwt_secret=strong,
            hook_secret=strong,
            kek=strong,
            database_url="postgresql+asyncpg://prod:x@db/lumen",
        )
    )
    async for client in make_client(app, reset=False):
        assert (await client.get("/api/v1/openapi.json")).status_code == 404
    dev = create_public_app(make_settings())
    async for client in make_client(dev):
        assert (await client.get("/api/v1/openapi.json")).status_code == 200
