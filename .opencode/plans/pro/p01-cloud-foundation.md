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

- [ ] `cloud/pyproject.toml` with pinned dependencies:
  - fastapi, uvicorn[standard], pydantic, pydantic-settings,
  - sqlalchemy[asyncio], asyncpg, alembic,
  - argon2-cffi, pyjwt, cryptography,
  - aiomqtt, redis, httpx, structlog, prometheus-client,
  - dev tools: pytest, pytest-asyncio, ruff, mypy.

  Lock the versions with `uv` or a pip-compile `requirements.lock`.
- [ ] Package layout:
  - `app/main.py` is the app factory,
  - `app/core/` holds settings, logging (secrets are redacted) and errors,
  - `app/db/` holds the engine and session, with a base model that has
    UUIDv7 primary keys and `created_at`/`updated_at`,
  - `app/api/v1/` holds the routers,
  - `app/services/`, `app/mqtt/` and `app/workers/`.
- [ ] Error envelope: `{"success","data","error":{"code","message"}}`, the
      same shape as the controller API, so the app unwraps it in one place.
- [ ] Settings from the environment only (`LUMEN_*`). `.env.example` holds
      dummy values. Startup fails if a production setting is missing or
      left at its default.
- [ ] Alembic setup and an initial empty revision. `make migrate`.
- [ ] `docker-compose.yml`:
  - api (hot reload),
  - postgres:16 with a volume,
  - emqx:5.8 (dashboard bound to localhost only; authn/authz hooks
    pointing at the API's internal port),
  - redis:7.
- [ ] Development TLS: a script generates a local CA and an EMQX server
      certificate into `cloud/dev-certs/`, which git ignores.
- [ ] `/healthz` (process) and `/readyz` (DB and Redis reachable).
      Prometheus `/metrics` on the internal port.
- [ ] `Makefile` targets: up, down, migrate, test, lint, fmt.

## Tests

- pytest runs against a dedicated test database, with each test in its own
  transaction (rolled back).
- Health checks, the error envelope and the settings validation all have
  tests.

## Gate

- `make lint` (ruff, mypy strict on `app/`) and `make test` pass.
- `docker compose up` reaches a healthy state on this machine, and `curl`
  gets 200 from `/readyz`. Recorded as "local docker".
