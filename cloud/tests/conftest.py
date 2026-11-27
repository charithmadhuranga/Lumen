"""Shared fixtures. Needs PostgreSQL and Redis from `make up` (127.0.0.1:55432 / :56379).

Tests use the `lumen_test` database and Redis DB 15. Migrations run once per
session; every client fixture empties all tables and the Redis DB first.
"""

from __future__ import annotations

import os
from collections.abc import AsyncIterator

import pytest
from alembic import command
from alembic.config import Config
from fastapi import FastAPI
from httpx import ASGITransport, AsyncClient

import app.db.models  # noqa: F401  - registers tables for the per-test reset
from app.core.settings import Settings
from app.main import create_internal_app, create_public_app

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_DB = os.environ.get("LUMEN_TEST_DATABASE_URL", "postgresql+asyncpg://lumen:lumen@127.0.0.1:55432/lumen_test")
TEST_REDIS = os.environ.get("LUMEN_TEST_REDIS_URL", "redis://127.0.0.1:56379/15")


def make_settings(**overrides: object) -> Settings:
    values: dict[str, object] = {
        "environment": "test",
        "database_url": TEST_DB,
        "redis_url": TEST_REDIS,
        "log_json": True,
        "readiness_timeout_s": 1.0,
    }
    values.update(overrides)
    return Settings(**values)  # type: ignore[arg-type]


def alembic_config(url: str = TEST_DB) -> Config:
    cfg = Config(os.path.join(HERE, "alembic.ini"))
    cfg.attributes["database_url"] = url
    return cfg


@pytest.fixture(scope="session")
def migrated() -> None:
    command.upgrade(alembic_config(), "head")


@pytest.fixture
def settings() -> Settings:
    return make_settings()


async def _reset_state(app: FastAPI) -> None:
    """Empty every table and the test Redis DB, so each test starts clean."""
    from app.db.base import Base

    # DELETE (children first) instead of TRUNCATE: on a handful of tiny tables it
    # is ~100x faster, because TRUNCATE rewrites every table's files.
    async with app.state.engine.begin() as conn:
        for table in reversed(Base.metadata.sorted_tables):
            await conn.execute(table.delete())
    await app.state.redis.flushdb()


async def _client(app: FastAPI, reset: bool = True) -> AsyncIterator[AsyncClient]:
    """reset=False for apps deliberately pointed at unreachable dependencies."""
    async with app.router.lifespan_context(app):
        if reset:
            await _reset_state(app)
        transport = ASGITransport(app=app, raise_app_exceptions=False)
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            yield client


@pytest.fixture
async def public_app(settings: Settings, migrated: None) -> FastAPI:
    return create_public_app(settings)


@pytest.fixture
async def client(public_app: FastAPI) -> AsyncIterator[AsyncClient]:
    async for c in _client(public_app):
        yield c


@pytest.fixture
async def internal_client(settings: Settings) -> AsyncIterator[AsyncClient]:
    async for c in _client(create_internal_app(settings)):
        yield c


@pytest.fixture
def make_client():  # type: ignore[no-untyped-def]
    """Build a client for a custom app (e.g. with test-only routes or other settings)."""
    return _client
