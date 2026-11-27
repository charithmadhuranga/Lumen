# Lumen Cloud

API-only backend for Lumen Pro (Python 3.12, FastAPI, PostgreSQL 16, EMQX 5.8, Redis 7): accounts, homes, the
device and gateway registry, claiming, broker auth/ACL hooks, command routing, state, events WebSocket and OTA
orchestration. No web frontend. Gateways are its only device clients (`docs/pro/DECISIONS.md` D-17).

Status: **P1 foundation done** (app factory, envelope, settings, logging, migrations, dev stack). Accounts and
homes come in P2. Plan: `.opencode/plans/pro/`.

## Develop

```sh
make setup        # uv sync --frozen (Python deps pinned in pyproject.toml + uv.lock)
make up           # creates .env and dev TLS certs, starts postgres, redis, emqx, api (waits for healthy)
make test         # pytest against the compose postgres/redis (DB lumen_test, Redis DB 15)
make lint         # ruff check + format check + mypy --strict
make down
```

| Service | Address (127.0.0.1 only) | Notes |
|---|---|---|
| Public API | http://127.0.0.1:8800 | `/healthz`, `/readyz`, `/api/v1/docs` (not in production) |
| Internal API | http://127.0.0.1:8801 | `/metrics`; EMQX hooks from P3. Never expose publicly |
| PostgreSQL | 127.0.0.1:55432 | `lumen`/`lumen` (development only) |
| Redis | 127.0.0.1:56379 | |
| EMQX (TLS) | mqtts://127.0.0.1:28883 | trusts `dev-certs/ca.pem`; refuses every client until P3 |
| EMQX dashboard | http://127.0.0.1:28083 | `admin` / `EMQX_DASHBOARD_PASSWORD` from `.env` |

`tools/dev_certs.sh <LAN IP>` adds a LAN address to the broker certificate (for a gateway on another machine).

## Layout

```text
app/core       settings (env only, production refuses unsafe values), envelope + errors, logging (redaction)
app/db         base model (UUIDv7 ids, timestamps), engine/session, models registry
app/api/v1     routers (health so far)
app/main.py    public + internal app factories, request logging, metrics, body limit (64 KB)
app/serve.py   runs both apps in one process (container entry point)
migrations/    Alembic (async); `make migrate`
tools/         dev_certs.sh, licenses.py (feeds THIRD_PARTY_NOTICES.md)
```

After changing dependencies run `uv run python tools/licenses.py` and `node ../scripts/third_party_notices.mjs`.
