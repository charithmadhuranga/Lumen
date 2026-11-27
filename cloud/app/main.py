"""Application factories.

Two ASGI apps share one process (app/serve.py):

- public  (LUMEN_PUBLIC_PORT, default 8800): /api/v1/* for the mobile app and
  gateways, plus /healthz and /readyz for the load balancer.
- internal (LUMEN_INTERNAL_PORT, default 8801): /metrics and, from P3, the
  EMQX auth/ACL hooks. Never exposed outside the private network.
"""

from __future__ import annotations

import time
import uuid
from collections.abc import AsyncIterator, Awaitable, Callable
from contextlib import AbstractAsyncContextManager, asynccontextmanager

import structlog
from fastapi import FastAPI, Request, Response
from prometheus_client import CONTENT_TYPE_LATEST, CollectorRegistry, Counter, Histogram, generate_latest
from redis.asyncio import Redis
from starlette.types import ASGIApp, Message, Receive, Scope, Send

from app.api.v1 import auth, health, homes
from app.core.errors import error_body, install_error_handlers
from app.core.logging import configure_logging
from app.core.settings import Settings, get_settings
from app.db.session import make_engine, make_sessionmaker
from app.services.email import DevEmailSender, EmailSender

log = structlog.get_logger("lumen.http")

MAX_BODY_BYTES = 64 * 1024  # per request; routes needing more (firmware upload, P9) get their own limit


class Metrics:
    def __init__(self) -> None:
        self.registry = CollectorRegistry()
        self.requests = Counter(
            "lumen_http_requests_total", "HTTP requests", ["route", "method", "status"], registry=self.registry
        )
        self.latency = Histogram(
            "lumen_http_request_seconds", "HTTP request latency", ["route", "method"], registry=self.registry
        )


class BodyLimit:
    """Reject request bodies over the limit (413) before any route runs.

    Content-Length is checked first. A body without one (chunked) is read into
    memory up to the limit, then replayed to the app, so the app never sees a
    partial body and a streamed upload cannot bypass the limit. Memory per
    request is bounded by the limit.
    """

    def __init__(self, app: ASGIApp, limit: int) -> None:
        self.app = app
        self.limit = limit

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        if scope["type"] != "http":
            await self.app(scope, receive, send)
            return
        for name, value in scope.get("headers", []):
            if name == b"content-length" and (not value.isdigit() or int(value) > self.limit):
                await _send_413(send, self.limit)
                return
        chunks: list[bytes] = []
        size = 0
        while True:
            message = await receive()
            if message["type"] == "http.disconnect":
                return
            body = message.get("body", b"")
            size += len(body)
            if size > self.limit:
                await _send_413(send, self.limit)
                return
            chunks.append(body)
            if not message.get("more_body", False):
                break
        replayed = False

        async def replay() -> Message:
            nonlocal replayed
            if not replayed:
                replayed = True
                return {"type": "http.request", "body": b"".join(chunks), "more_body": False}
            return await receive()  # after the body: wait for disconnect, as Starlette expects

        await self.app(scope, replay, send)


async def _send_413(send: Send, limit: int) -> None:
    import json

    body = json.dumps(error_body("PAYLOAD_TOO_LARGE", f"Request body exceeds {limit} bytes")).encode()
    await send(
        {
            "type": "http.response.start",
            "status": 413,
            "headers": [(b"content-type", b"application/json"), (b"content-length", str(len(body)).encode())],
        }
    )
    await send({"type": "http.response.body", "body": body})


def _route_label(request: Request) -> str:
    route = request.scope.get("route")
    return getattr(route, "path", "unmatched")


def _install_common(app: FastAPI, metrics: Metrics) -> None:
    install_error_handlers(app)

    @app.middleware("http")
    async def _observe(request: Request, call_next: Callable[[Request], Awaitable[Response]]) -> Response:
        request_id = request.headers.get("x-request-id", "")[:64] or uuid.uuid4().hex
        structlog.contextvars.bind_contextvars(request_id=request_id)
        start = time.perf_counter()
        status = 500
        try:
            response = await call_next(request)
            status = response.status_code
        finally:
            elapsed = time.perf_counter() - start
            route = _route_label(request)
            metrics.requests.labels(route, request.method, str(status)).inc()
            metrics.latency.labels(route, request.method).observe(elapsed)
            # Path only: query strings can carry tokens (events WebSocket, P4).
            log.info(
                "request", method=request.method, path=request.url.path, status=status, ms=round(elapsed * 1000, 1)
            )
            structlog.contextvars.unbind_contextvars("request_id")
        response.headers["X-Request-Id"] = request_id
        response.headers["Cache-Control"] = "no-store"
        response.headers["X-Content-Type-Options"] = "nosniff"
        return response


class Resources:
    """Database engine, session factory and Redis client.

    serve.py creates one instance for both apps and owns its lifecycle; an app
    built without one (tests, `uvicorn app.main:...`) creates and closes its own.
    """

    def __init__(self, settings: Settings) -> None:
        self.engine = make_engine(settings)
        self.sessionmaker = make_sessionmaker(self.engine)
        self.redis: Redis = Redis.from_url(
            settings.redis_url.get_secret_value(), socket_timeout=settings.readiness_timeout_s
        )

    async def close(self) -> None:
        await self.redis.aclose()
        await self.engine.dispose()


def _lifespan(settings: Settings, shared: Resources | None) -> Callable[[FastAPI], AbstractAsyncContextManager[None]]:
    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        resources = shared or Resources(settings)
        app.state.engine = resources.engine
        app.state.sessionmaker = resources.sessionmaker
        app.state.redis = resources.redis
        try:
            yield
        finally:
            if shared is None:
                await resources.close()

    return lifespan


def create_public_app(
    settings: Settings | None = None,
    metrics: Metrics | None = None,
    resources: Resources | None = None,
    email: EmailSender | None = None,
) -> FastAPI:
    settings = settings or get_settings()
    metrics = metrics or Metrics()
    configure_logging(settings.log_level, settings.log_json)
    app = FastAPI(
        title="Lumen Cloud API",
        version="1",
        lifespan=_lifespan(settings, resources),
        openapi_url="/api/v1/openapi.json" if settings.environment != "production" else None,
        docs_url="/api/v1/docs" if settings.environment != "production" else None,
        redoc_url=None,
    )
    app.state.settings = settings
    app.state.metrics = metrics
    app.state.email = email or DevEmailSender()
    _install_common(app, metrics)
    app.include_router(health.router)
    app.include_router(auth.router)
    app.include_router(homes.router)
    app.add_middleware(BodyLimit, limit=MAX_BODY_BYTES)
    return app


def create_internal_app(
    settings: Settings | None = None, metrics: Metrics | None = None, resources: Resources | None = None
) -> FastAPI:
    settings = settings or get_settings()
    metrics = metrics or Metrics()
    app = FastAPI(
        title="Lumen Cloud internal",
        lifespan=_lifespan(settings, resources),
        openapi_url=None,
        docs_url=None,
        redoc_url=None,
    )
    app.state.settings = settings
    app.state.metrics = metrics
    _install_common(app, metrics)

    @app.get("/healthz")
    async def internal_healthz() -> dict[str, str]:
        return {"status": "ok"}

    @app.get("/metrics")
    async def prometheus_metrics() -> Response:
        return Response(generate_latest(metrics.registry), media_type=CONTENT_TYPE_LATEST)

    app.add_middleware(BodyLimit, limit=MAX_BODY_BYTES)
    return app
