# P1 — Cloud foundation

## Outcome

`cd cloud && make up` starts the development stack:

- API,
- PostgreSQL,
- EMQX,
- Redis.

The API serves `/healthz` and `/readyz`. Migrations run, and the test suite
runs against a real Postgres.

## Tasks

- [x] `cloud/pyproject.toml` with pinned dependencies:
  - fastapi, uvicorn[standard], pydantic, pydantic-settings,
  - sqlalchemy[asyncio], asyncpg, alembic,
  - argon2-cffi, pyjwt, cryptography,
  - aiomqtt, redis, httpx, structlog, prometheus-client,
  - dev tools: pytest, pytest-asyncio, ruff, mypy.

  Lock the versions with `uv` or a pip-compile `requirements.lock`.
- [x] Package layout:
  - `app/main.py` is the app factory,
  - `app/core/` holds settings, logging (secrets are redacted) and errors,
  - `app/db/` holds the engine and session, with a base model that has
    UUIDv7 primary keys and `created_at`/`updated_at`,
  - `app/api/v1/` holds the routers,
  - `app/services/`, `app/mqtt/` and `app/workers/`: created when their first
    module lands (P2–P4), not as empty packages.
- [x] Error envelope: `{"success","data","error":{"code","message"}}`, the
      same shape as the controller API, so the app unwraps it in one place.
- [x] Settings from the environment only (`LUMEN_*`). `.env.example` holds
      dummy values. Startup fails if a production setting is missing or
      left at its default.
- [x] Alembic setup and an initial empty revision. `make migrate`.
- [x] `docker-compose.yml`:
  - api: runs the built image, no hot reload (`uv run uvicorn` locally for
    that);
  - postgres:16 with a volume,
  - emqx:5.8 (dashboard bound to localhost only). The authn/authz hooks are
    pointed at the API in **P3**; until then authentication is an empty
    built-in database and authorization denies when nothing matches, so
    every client is refused,
  - redis:7.
- [x] Development TLS: a script generates a local CA and an EMQX server
      certificate into `cloud/dev-certs/`, which git ignores.
- [x] `/healthz` (process) and `/readyz` (DB and Redis reachable).
      Prometheus `/metrics` on the internal port.
- [x] `Makefile` targets: up, down, migrate, test, lint, fmt.

## Tests

- pytest runs against a dedicated test database. Rolled-back per-test
  transactions arrive with the first tables in P2.
- Health checks, the error envelope and the settings validation all have
  tests.

## Gate

- `make lint` (ruff, mypy strict on `app/`) and `make test` pass.
- `docker compose up` reaches a healthy state on this machine, and `curl`
  gets 200 from `/readyz`. Recorded as "local docker".

## Result (2026-11-27)

Gate passed: ruff and mypy --strict clean; 35 tests (health, readiness with failing dependencies, envelope and
error mapping, body limit incl. chunked bodies, security headers, production settings refusal, UUIDv7, log
redaction, internal metrics, migrations down/up and model/migration parity, broker refuses unknown and anonymous
clients and serves TLS only) against the compose Postgres/Redis/EMQX; `make up` reaches healthy and
`/readyz` returns 200 (local docker).

Notes: plain-TCP/WebSocket listeners are disabled on EMQX (TLS 8883 only); EMQX node is `emqx@127.0.0.1`
(single-node dev). Tables start in P2 (baseline migration only enables `citext`).
