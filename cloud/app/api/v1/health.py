"""Liveness and readiness.

/healthz: the process answers (no dependencies).
/readyz:  PostgreSQL and Redis both answer within the readiness timeout;
          otherwise 503 with the failing dependency names (never connection
          strings or error text that could contain credentials).
"""

from __future__ import annotations

import asyncio
from typing import Any

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse
from sqlalchemy import text

from app.core.errors import error_body, ok

router = APIRouter(tags=["health"])


@router.get("/healthz")
async def healthz() -> dict[str, Any]:
    return ok({"status": "ok"})


async def _check_db(request: Request) -> None:
    async with request.app.state.engine.connect() as conn:
        await conn.execute(text("SELECT 1"))


async def _check_redis(request: Request) -> None:
    await request.app.state.redis.ping()


@router.get("/readyz", response_model=None)
async def readyz(request: Request) -> dict[str, Any] | JSONResponse:
    timeout = request.app.state.settings.readiness_timeout_s
    checks = {"database": _check_db(request), "redis": _check_redis(request)}
    results = await asyncio.gather(*(asyncio.wait_for(c, timeout) for c in checks.values()), return_exceptions=True)
    status = {
        name: ("ok" if not isinstance(r, BaseException) else "unavailable")
        for name, r in zip(checks, results, strict=True)
    }
    failing = [name for name, s in status.items() if s != "ok"]
    if failing:
        body = error_body("UNAVAILABLE", "not ready: " + ", ".join(failing))
        body["data"] = {"checks": status}
        return JSONResponse(body, status_code=503)
    return ok({"checks": status})
