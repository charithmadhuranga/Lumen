"""Remote commands and device status (P4). Logic: app/services/commands.py."""

from __future__ import annotations

import uuid
from datetime import datetime
from typing import Annotated, Any

from fastapi import APIRouter, Header, Request, status
from pydantic import Field, StringConstraints
from sqlalchemy import select

from app.api.deps import CurrentPrincipal, DbSession, check, load_home_access
from app.api.schemas import ApiModel, Envelope
from app.api.v1.devices import _device_access
from app.core.errors import ApiError, ok
from app.core.permissions import Action
from app.models.messaging import Command, DeviceConfig, DeviceState
from app.services import commands as svc

router = APIRouter(prefix="/api/v1", tags=["commands"])


class CommandRequest(ApiModel):
    op: Annotated[str, StringConstraints(max_length=24)]
    target: Annotated[str, StringConstraints(max_length=48)] | None = None
    args: dict[str, Any] | None = None
    ttl_seconds: Annotated[int, Field(ge=1, le=svc.MAX_TTL_S)] | None = None


class CommandOut(ApiModel):
    id: uuid.UUID
    device_id: uuid.UUID
    op: str
    target: str | None
    args: dict[str, Any] | None
    status: str
    code: str | None
    message: str | None
    created_at: datetime
    expires_at: datetime
    sent_at: datetime | None
    completed_at: datetime | None


class DeviceStatusOut(ApiModel):
    device_id: uuid.UUID
    online: bool
    last_seen_at: datetime | None
    state: dict[str, Any] | None
    config_rev: int | None
    config: dict[str, Any] | None


def command_out(c: Command) -> dict[str, Any]:
    return CommandOut(
        id=c.id,
        device_id=c.device_id,
        op=c.op,
        target=c.target,
        args=c.args,
        status=c.status.value,
        code=c.code,
        message=c.message,
        created_at=c.created_at,
        expires_at=c.expires_at,
        sent_at=c.sent_at,
        completed_at=c.completed_at,
    ).model_dump(mode="json", by_alias=True)


@router.post("/devices/{device_id}/commands", status_code=status.HTTP_202_ACCEPTED, response_model=Envelope[CommandOut])
async def send_command(
    device_id: uuid.UUID,
    body: CommandRequest,
    principal: CurrentPrincipal,
    request: Request,
    db: DbSession,
    idempotency_key: Annotated[str | None, Header(max_length=64, pattern=r"^[A-Za-z0-9_-]{8,64}$")] = None,
) -> dict[str, Any]:
    device, access = await _device_access(db, principal, device_id)
    check(access, Action.control)
    command = await svc.create(
        db,
        request.app.state.publisher,
        request.app.state.redis,
        principal.user,
        access.role,
        device,
        body.op,
        body.target,
        body.args,
        body.ttl_seconds,
        idempotency_key,
    )
    return ok(command_out(command))


@router.get("/commands/{command_id}", response_model=Envelope[CommandOut])
async def get_command(command_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    command = await db.get(Command, command_id)
    if command is None:
        raise ApiError(404, "NOT_FOUND", "Command not found")
    try:
        access = await load_home_access(db, principal.user, command.home_id)
    except ApiError:
        raise ApiError(404, "NOT_FOUND", "Command not found") from None
    check(access, Action.control)
    return ok(command_out(command))


@router.get("/devices/{device_id}/commands", response_model=Envelope[list[CommandOut]])
async def list_commands(device_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    device, access = await _device_access(db, principal, device_id)
    check(access, Action.control)
    rows = await db.scalars(
        select(Command).where(Command.device_id == device.id).order_by(Command.created_at.desc()).limit(50)
    )
    return ok([command_out(c) for c in rows])


@router.get("/devices/{device_id}/status", response_model=Envelope[DeviceStatusOut])
async def device_status(device_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    device, access = await _device_access(db, principal, device_id)
    check(access, Action.home_read)
    state = await db.get(DeviceState, device.id)
    config = await db.get(DeviceConfig, device.id)
    return ok(
        DeviceStatusOut(
            device_id=device.id,
            online=bool(state and state.online),
            last_seen_at=device.last_seen_at,
            state=state.reported if state else None,
            config_rev=config.rev if config else None,
            config=config.doc if config else None,
        ).model_dump(mode="json", by_alias=True)
    )
