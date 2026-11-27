"""Settings safety, UUIDv7, log redaction, internal app, migrations."""

from __future__ import annotations

import json
import time
import uuid

import pytest
import structlog
from alembic import command
from alembic.autogenerate import compare_metadata
from alembic.migration import MigrationContext
from httpx import AsyncClient
from pydantic import ValidationError
from sqlalchemy.ext.asyncio import create_async_engine

from app.core.logging import REDACTED, configure_logging, redact_processor
from app.core.settings import DEV_SECRET, Settings
from app.db.base import Base, uuid7
from tests.conftest import TEST_DB, alembic_config

STRONG = "q" * 40
PROD = {
    "environment": "production",
    "jwt_secret": STRONG,
    "hook_secret": STRONG,
    "kek": STRONG,
    "emqx_api_secret": STRONG,
    "mqtt_service_password": STRONG,
    "database_url": "postgresql+asyncpg://prod:x@db/lumen",
}

# --- settings -----------------------------------------------------------------


def test_development_defaults_start() -> None:
    assert Settings(environment="development").jwt_secret.get_secret_value()


def test_production_with_strong_values_starts() -> None:
    Settings(**PROD)  # type: ignore[arg-type]


@pytest.mark.parametrize(
    ("override", "fragment"),
    [
        ({"jwt_secret": DEV_SECRET}, "LUMEN_JWT_SECRET"),
        ({"hook_secret": "short"}, "LUMEN_HOOK_SECRET"),
        ({"kek": DEV_SECRET}, "LUMEN_KEK"),
        ({"emqx_api_secret": "short"}, "LUMEN_EMQX_API_SECRET"),
        ({"mqtt_service_password": DEV_SECRET}, "LUMEN_MQTT_SERVICE_PASSWORD"),
        ({"database_url": "postgresql+asyncpg://lumen:lumen@db/lumen"}, "LUMEN_DATABASE_URL"),
        ({"broker_url": "mqtt://broker:1883"}, "mqtts://"),
        ({"log_level": "DEBUG"}, "DEBUG"),
    ],
)
def test_production_refuses_unsafe_settings(override: dict[str, str], fragment: str) -> None:
    with pytest.raises(ValidationError) as err:
        Settings(**{**PROD, **override})  # type: ignore[arg-type]
    assert fragment in str(err.value)
    assert STRONG not in str(err.value)


def test_secrets_are_not_shown_in_repr() -> None:
    s = Settings(**PROD)  # type: ignore[arg-type]
    assert STRONG not in repr(s)
    assert "prod:x" not in repr(s)


# --- uuid7 --------------------------------------------------------------------


def test_uuid7_version_variant_and_time() -> None:
    before = time.time_ns() // 1_000_000
    u = uuid7()
    after = time.time_ns() // 1_000_000
    assert u.version == 7
    assert u.variant == uuid.RFC_4122
    assert before <= (u.int >> 80) <= after


def test_uuid7_is_time_ordered_and_unique() -> None:
    first = uuid7()
    time.sleep(0.002)
    ids = [uuid7() for _ in range(5000)]
    assert len(set(ids)) == len(ids)
    assert all(first < u for u in ids)


def test_uuid7_matches_protocol_pattern() -> None:
    import re

    pattern = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
    assert all(pattern.match(str(uuid7())) for _ in range(1000))


# --- logging ------------------------------------------------------------------


def test_redaction_of_secret_keys_at_any_depth() -> None:
    event = {
        "event": "x",
        "password": "hunter2",
        "user": {"email": "a@b.c", "refresh_token": "rt", "nested": [{"localKey": "k", "ok": 1}]},
        "Authorization": "Bearer abc",
        "claimToken": "lct_x",
        "count": 3,
    }
    out = redact_processor(None, "info", dict(event))
    assert out["password"] == REDACTED
    assert out["Authorization"] == REDACTED
    assert out["claimToken"] == REDACTED
    assert out["user"]["refresh_token"] == REDACTED
    assert out["user"]["nested"][0] == {"localKey": REDACTED, "ok": 1}
    assert out["user"]["email"] == "a@b.c"
    assert out["count"] == 3


def test_configured_logger_never_prints_secret(capsys: pytest.CaptureFixture[str]) -> None:
    configure_logging("INFO", json_output=True)
    structlog.get_logger("t").info("login", password="hunter2", mqtt={"password": "p"})
    line = capsys.readouterr().out.strip().splitlines()[-1]
    data = json.loads(line)
    assert "hunter2" not in line
    assert data["mqtt"]["password"] == REDACTED


# --- internal app ---------------------------------------------------------------


async def test_internal_metrics_and_health(internal_client: AsyncClient) -> None:
    assert (await internal_client.get("/healthz")).status_code == 200
    r = await internal_client.get("/metrics")
    assert r.status_code == 200
    assert "lumen_http_requests_total" in r.text
    assert 'route="/healthz"' in r.text


# --- migrations ---------------------------------------------------------------


def test_migrations_downgrade_and_upgrade_cleanly(migrated: None) -> None:
    cfg = alembic_config()
    command.downgrade(cfg, "base")
    command.upgrade(cfg, "head")


async def test_models_match_migrations(migrated: None) -> None:
    engine = create_async_engine(TEST_DB)
    async with engine.connect() as conn:
        diff = await conn.run_sync(
            lambda sync: compare_metadata(MigrationContext.configure(sync, opts={"compare_type": True}), Base.metadata)
        )
    await engine.dispose()
    assert diff == [], f"models and migrations differ; generate a revision: {diff}"
