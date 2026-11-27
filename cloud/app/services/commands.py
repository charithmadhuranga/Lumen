"""Remote commands (PROTOCOL.md §3-4).

A command is checked against the caller's role and the controller's reported
capabilities, stored as `sent`, then published to `lumen/v1/d/{id}/cmd` with an
MQTT 5 expiry equal to its TTL (`failed` if the broker cannot take it). Only a device-level result
(`applied`, `rejected`, ...) or the sweeper (`expired`) finishes it. Commands
for an offline device are refused, never queued for later (brief §9.7).
"""

from __future__ import annotations

import time
import uuid
from datetime import UTC, datetime, timedelta
from typing import Any

import structlog
from pydantic import ValidationError
from redis.asyncio import Redis
from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app import protocol
from app.core.errors import ApiError
from app.core.permissions import Action, allowed
from app.db.base import uuid7
from app.models.account import User
from app.models.device import Device, DeviceKind
from app.models.home import Role
from app.models.messaging import Command, CommandStatus, DeviceConfig, DeviceState
from app.mqtt.bridge import BridgeUnavailable, Publisher
from app.services.events import publish_event
from app.services.ingest import command_event

log = structlog.get_logger("lumen.commands")

DEFAULT_TTL_S = 15
MAX_TTL_S = 60
ADMIN_OPS = {"device.restart", "config.apply"}


def _now() -> datetime:
    return datetime.now(UTC)


def _ms(t: datetime) -> int:
    return int(t.timestamp() * 1000)


async def _online(db: AsyncSession, device_id: uuid.UUID) -> bool:
    state = await db.get(DeviceState, device_id)
    return bool(state and state.online)


def _check_capabilities(op: str, target: str | None, args: dict[str, Any] | None, config: dict[str, Any]) -> None:
    def unknown(what: str) -> ApiError:
        return ApiError(404, "UNKNOWN_TARGET", f"This controller has no {what} '{target}'")

    if op in ("light.set", "light.effect"):
        light = next((x for x in config["lights"] if x["id"] == target), None)
        if light is None:
            raise unknown("light")
        sup = light["supports"]
        a = args or {}
        if op == "light.effect":
            if a.get("effect") not in (*sup["effects"], "stop", "none"):
                raise ApiError(422, "UNSUPPORTED_PROPERTY", "This light has no such effect")
            return
        needs = [
            ("brightness", "brightness", ("brightness",)),
            ("color", "colour", ("red", "green", "blue")),
            ("white", "a white channel", ("white",)),
            ("colorTemperature", "colour temperature", ("colorTemperature",)),
        ]
        for flag, label, keys in needs:
            if any(k in a for k in keys) and not sup[flag]:
                raise ApiError(422, "UNSUPPORTED_PROPERTY", f"This light has no {label}")
        if "colorTemperature" in a:
            low, high = sup.get("kelvinMin", 1000), sup.get("kelvinMax", 20000)
            if not low <= int(a["colorTemperature"]) <= high:
                raise ApiError(422, "UNSUPPORTED_PROPERTY", f"Colour temperature must be {low}-{high} K")
    elif op == "group.set" and not any(x["id"] == target for x in config["groups"]):
        raise unknown("group")
    elif op == "room.set" and not any(x["id"] == target for x in config["rooms"]):
        raise unknown("room")
    elif op == "scene.activate" and not any(x["id"] == target for x in config["scenes"]):
        raise unknown("scene")


async def create(
    db: AsyncSession,
    publisher: Publisher,
    redis: Redis,
    user: User,
    role: Role,
    device: Device,
    op: str,
    target: str | None,
    args: dict[str, Any] | None,
    ttl_s: int | None,
    idempotency_key: str | None,
) -> Command:
    if idempotency_key:
        existing = (
            await db.execute(
                select(Command).where(Command.user_id == user.id, Command.idempotency_key == idempotency_key)
            )
        ).scalar_one_or_none()
        if existing is not None:
            if existing.device_id != device.id or existing.op != op:
                raise ApiError(409, "CONFLICT", "This Idempotency-Key was used for a different command")
            return existing
    if op in ADMIN_OPS and not allowed(role, Action.devices_manage):
        raise ApiError(403, "FORBIDDEN", "Only owners and admins can do this")
    if op == "config.apply":
        raise ApiError(400, "NOT_AVAILABLE", "Editing configuration from the cloud arrives with configuration sync")
    if device.kind != DeviceKind.controller:
        raise ApiError(400, "INVALID_REQUEST", "Commands are for controllers")
    if device.gateway_id is None or not await _online(db, device.gateway_id) or not await _online(db, device.id):
        raise ApiError(409, "DEVICE_OFFLINE", "The controller is not reachable right now")

    t0 = time.perf_counter()
    now = _now()
    ttl = ttl_s or DEFAULT_TTL_S
    command_id = uuid7()
    wire: dict[str, Any] = {
        "v": 1,
        "id": str(command_id),
        "ts": _ms(now),
        "exp": _ms(now + timedelta(seconds=ttl)),
        "src": {"kind": "user", "id": str(user.id)},
        "op": op,
    }
    if target is not None:
        wire["target"] = target
    if args is not None:
        wire["args"] = args
    try:
        protocol.Command.model_validate(wire)
    except ValidationError as exc:
        first = exc.errors()[0]
        raise ApiError(400, "INVALID_COMMAND", str(first.get("msg", "invalid command"))[:160]) from None
    if op in protocol.TARGETED:
        config = await db.get(DeviceConfig, device.id)
        if config is None:
            raise ApiError(409, "CONFIG_UNKNOWN", "The controller has not reported its configuration yet")
        _check_capabilities(op, target, args, config.doc)

    # Stored as `sent` before publishing: nothing is written after the publish, so a fast
    # device's result (ingested concurrently) can never be overwritten, and the request
    # never waits on the result's row lock. If publishing fails the command becomes `failed`.
    command = Command(
        id=command_id,
        device_id=device.id,
        home_id=device.home_id,
        user_id=user.id,
        op=op,
        target=target,
        args=args,
        status=CommandStatus.sent,
        idempotency_key=idempotency_key,
        expires_at=now + timedelta(seconds=ttl),
        sent_at=now,
    )
    db.add(command)
    await db.commit()
    await publish_event(redis, device.home_id, "command.updated", command_event(command))
    t_stored = time.perf_counter()
    try:
        await publisher.publish(f"lumen/v1/d/{device.id}/cmd", wire, expiry_s=ttl)
    except BridgeUnavailable:
        failed = (
            await db.execute(
                update(Command)
                .where(Command.id == command.id, Command.status == CommandStatus.sent)
                .values(status=CommandStatus.failed, code="broker_unavailable", completed_at=_now())
                .returning(Command)
            )
        ).scalar_one_or_none()
        await db.commit()
        if failed is not None:
            await publish_event(redis, device.home_id, "command.updated", command_event(failed))
        raise ApiError(503, "UNAVAILABLE", "The cloud cannot reach the devices right now; try again") from None
    log.info(
        "command_sent",
        command_id=str(command.id),
        op=op,
        validate_store_ms=round((t_stored - t0) * 1000, 1),
        publish_ms=round((time.perf_counter() - t_stored) * 1000, 1),
    )
    return command
