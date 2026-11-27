"""Small helpers for API tests."""

from __future__ import annotations

from typing import Any

from httpx import AsyncClient, Response

PASSWORD = "correct horse battery"


def data(r: Response, status: int = 200) -> Any:
    assert r.status_code == status, r.text
    body = r.json()
    assert body["success"] is True
    return body["data"]


def error(r: Response, status: int, code: str) -> dict[str, Any]:
    assert r.status_code == status, r.text
    body = r.json()
    assert body["success"] is False
    assert body["error"]["code"] == code
    return body["error"]  # type: ignore[no-any-return]


def bearer(tokens: dict[str, Any]) -> dict[str, str]:
    return {"Authorization": f"Bearer {tokens['accessToken']}"}


async def register(client: AsyncClient, email: str, password: str = PASSWORD) -> dict[str, Any]:
    return data(await client.post("/api/v1/auth/register", json={"email": email, "password": password}), 201)  # type: ignore[no-any-return]


async def login(client: AsyncClient, email: str, password: str = PASSWORD) -> dict[str, Any]:
    return data(await client.post("/api/v1/auth/login", json={"email": email, "password": password}))  # type: ignore[no-any-return]


async def make_home(client: AsyncClient, tokens: dict[str, Any], name: str = "Home") -> dict[str, Any]:
    r = await client.post("/api/v1/homes", json={"name": name, "timezone": "Asia/Colombo"}, headers=bearer(tokens))
    return data(r, 201)  # type: ignore[no-any-return]
