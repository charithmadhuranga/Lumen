"""EMQX authentication and authorisation hooks (PROTOCOL.md §7). Internal port only.

Both answer 200 with {"result": "allow" | "deny"} so EMQX never falls through
to another source, and both require X-Lumen-Hook-Secret. Any error denies.
"""

from __future__ import annotations

import hmac
from typing import Any, Literal

import structlog
from fastapi import APIRouter, Request
from pydantic import BaseModel, Field
from sqlalchemy import select

from app.api.deps import DbSession
from app.core.errors import ApiError
from app.core.security import token_digest
from app.models.device import Device, DeviceKind
from app.services import broker
from app.services import devices as svc

log = structlog.get_logger("lumen.mqtt.hooks")
router = APIRouter(prefix="/internal/mqtt", tags=["internal"])

ALLOW: dict[str, Any] = {"result": "allow", "is_superuser": False}
DENY: dict[str, Any] = {"result": "deny"}


class AuthRequest(BaseModel):
    clientid: str = Field(max_length=64)
    username: str = Field(default="", max_length=64)
    password: str = Field(default="", max_length=128)


class AclRequest(BaseModel):
    clientid: str = Field(max_length=64)
    username: str = Field(default="", max_length=64)
    topic: str = Field(max_length=256)
    action: Literal["publish", "subscribe", "all"]


def _check_secret(request: Request) -> None:
    expected = request.app.state.settings.hook_secret.get_secret_value().encode()
    given = request.headers.get("x-lumen-hook-secret", "").encode()
    if not hmac.compare_digest(given, expected):
        raise ApiError(403, "FORBIDDEN", "Bad hook secret")


@router.post("/auth")
async def mqtt_auth(body: AuthRequest, request: Request, db: DbSession) -> dict[str, Any]:
    _check_secret(request)
    client = broker.parse_client(body.clientid, body.username)
    if client is None or not body.password:
        return DENY
    if client.kind == "service":
        expected = request.app.state.settings.mqtt_service_password.get_secret_value()
        return ALLOW if hmac.compare_digest(token_digest(body.password), token_digest(expected)) else DENY
    assert client.gateway_id is not None
    gateway = await svc.authenticate_gateway(db, client.gateway_id, body.password)
    if gateway is None:
        log.info("mqtt_auth_denied", clientid=body.clientid)
        return DENY
    return ALLOW


@router.post("/acl")
async def mqtt_acl(body: AclRequest, request: Request, db: DbSession) -> dict[str, Any]:
    _check_secret(request)
    client = broker.parse_client(body.clientid, body.username)
    if client is None or body.action == "all":
        return DENY
    if client.kind == "service":
        return ALLOW if broker.service_allowed(body.topic, body.action) else DENY
    assert client.gateway_id is not None
    children: set[Any] = set()
    if (device_id := broker.wanted_topic_ids(body.topic)) is not None:
        row = (
            await db.execute(
                select(Device.id).where(
                    Device.id == device_id,
                    Device.kind == DeviceKind.controller,
                    Device.gateway_id == client.gateway_id,
                )
            )
        ).scalar_one_or_none()
        if row is not None:
            children.add(row)
    # A gateway that was released (row gone) is refused by authentication on its
    # next connect and kicked at release; the topic rules still only allow its own ids.
    return ALLOW if broker.gateway_allowed(client.gateway_id, children, body.topic, body.action) else DENY
