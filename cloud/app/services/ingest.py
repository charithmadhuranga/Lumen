"""Device traffic -> database rows and live events (PROTOCOL.md §2, §4, §5).

Every payload is validated with app/protocol.py; invalid ones are counted and
dropped, never partly applied. The broker ACL already guarantees the topic is
one the sending gateway may use; the payload's `device` must also equal the
topic's device id. State and config only move forward in device time, so
re-delivered or reordered messages (QoS 1, several instances) cannot roll
them back.
"""

from __future__ import annotations

import asyncio
import json
import re
import uuid
from collections import Counter
from datetime import UTC, datetime
from typing import Any

import structlog
from pydantic import ValidationError
from redis.asyncio import Redis
from sqlalchemy import select, text, update
from sqlalchemy.dialects.postgresql import insert
from sqlalchemy.exc import DBAPIError
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker

from app import protocol
from app.models.device import Device, DeviceKind
from app.models.messaging import TERMINAL, Command, CommandStatus, DeviceConfig, DeviceEvent, DeviceState
from app.services.events import publish_event

log = structlog.get_logger("lumen.ingest")

_TOPIC = re.compile(
    r"^lumen/v1/([dg])/([0-9a-f-]{36})/(availability|state|cmd/result|config/reported|events|telemetry|status)$"
)
stats: Counter[str] = Counter()


def _now() -> datetime:
    return datetime.now(UTC)


class Ingestor:
    def __init__(self, sessionmaker: async_sessionmaker[AsyncSession], redis: Redis) -> None:
        self.sessionmaker = sessionmaker
        self.redis = redis

    async def handle(self, topic: str, payload: bytes, retained: bool = False) -> None:
        m = _TOPIC.match(topic)
        if m is None:
            stats["bad_topic"] += 1
            return
        scope, raw_id, kind = m.groups()
        try:
            device_id = uuid.UUID(raw_id)
        except ValueError:
            stats["bad_topic"] += 1
            return
        handler = getattr(self, f"_{scope}_{kind.replace('/', '_')}")
        for attempt in range(1, RETRIES + 1):
            try:
                async with self.sessionmaker() as db:
                    # Device reports are re-sent (retained state, periodic reports), so ingestion
                    # does not wait for the WAL flush; commands and accounts stay fully durable.
                    await db.execute(text("SET LOCAL synchronous_commit = off"))
                    # Lock the device row first. Deleting a home locks homes -> devices -> states;
                    # taking the device row before touching its state keeps the same order, so the
                    # two wait for each other instead of deadlocking.
                    device = await db.get(Device, device_id, with_for_update={"key_share": True})
                    expected = DeviceKind.gateway if scope == "g" else DeviceKind.controller
                    if device is None or device.kind != expected:
                        stats["unknown_device"] += 1
                        return
                    try:
                        await handler(db, device, payload)
                    except (ValidationError, ValueError, UnicodeDecodeError) as exc:
                        stats["invalid"] += 1
                        log.info("invalid_payload", topic=topic, error=type(exc).__name__)
                        return
                stats[f"{scope}/{kind}"] += 1
                return
            except DBAPIError as exc:
                if not _retryable(exc) or attempt == RETRIES:
                    raise
                stats["retried"] += 1
                await asyncio.sleep(0.05 * attempt)

    # --- availability -----------------------------------------------------------------------

    async def _set_online(self, db: AsyncSession, device: Device, online: bool) -> bool:
        row = await db.get(DeviceState, device.id)
        changed = row is None or row.online != online
        stmt = insert(DeviceState).values(device_id=device.id, online=online, availability_at=_now())
        await db.execute(
            stmt.on_conflict_do_update(
                index_elements=[DeviceState.device_id], set_={"online": online, "availability_at": _now()}
            )
        )
        if online:
            device.last_seen_at = _now()
        return changed

    async def _d_availability(self, db: AsyncSession, device: Device, payload: bytes) -> None:
        online = _availability(payload)
        changed = await self._set_online(db, device, online)
        await db.commit()
        if changed:
            await self._emit(device, "device.online" if online else "device.offline", {"deviceId": str(device.id)})

    async def _g_availability(self, db: AsyncSession, device: Device, payload: bytes) -> None:
        online = _availability(payload)
        changed = await self._set_online(db, device, online)
        went_offline: list[uuid.UUID] = []
        if not online:
            # The broker's Last Will covers only the gateway; its children go with it (PROTOCOL §5).
            children = (await db.scalars(select(Device.id).where(Device.gateway_id == device.id))).all()
            for child in children:
                state = await db.get(DeviceState, child)
                if state is not None and state.online:
                    state.online, state.availability_at = False, _now()
                    went_offline.append(child)
        await db.commit()
        if changed:
            await self._emit(device, "device.online" if online else "device.offline", {"deviceId": str(device.id)})
        for child in went_offline:
            await self._emit(device, "device.offline", {"deviceId": str(child), "reason": "gateway_offline"})

    # --- state and config ----------------------------------------------------------------------

    async def _d_state(self, db: AsyncSession, device: Device, payload: bytes) -> None:
        state = protocol.State.model_validate_json(payload)
        _same_device(state.device, device)
        body = state.wire()
        stmt = insert(DeviceState).values(device_id=device.id, reported=body, reported_ts=state.ts)
        result = await db.execute(
            stmt.on_conflict_do_update(
                index_elements=[DeviceState.device_id],
                set_={"reported": body, "reported_ts": state.ts},
                where=(DeviceState.reported_ts.is_(None)) | (DeviceState.reported_ts <= state.ts),
            )
        )
        device.last_seen_at = _now()
        await db.commit()
        if result.rowcount:  # type: ignore[attr-defined]
            await self._emit(
                device,
                "device.state",
                {
                    "deviceId": str(device.id),
                    "ts": state.ts,
                    "lights": body["lights"],
                    "inputs": body.get("inputs", []),
                },
            )
        else:
            stats["stale_state"] += 1

    async def _d_config_reported(self, db: AsyncSession, device: Device, payload: bytes) -> None:
        cfg = protocol.ConfigReported.model_validate_json(payload)
        _same_device(cfg.device, device)
        body = cfg.wire()
        stmt = insert(DeviceConfig).values(device_id=device.id, rev=cfg.rev, doc=body, reported_ts=cfg.ts)
        result = await db.execute(
            stmt.on_conflict_do_update(
                index_elements=[DeviceConfig.device_id],
                set_={"rev": cfg.rev, "doc": body, "reported_ts": cfg.ts},
                where=DeviceConfig.reported_ts <= cfg.ts,
            )
        )
        if result.rowcount:  # type: ignore[attr-defined]
            device.fw_version, device.board = cfg.fw, cfg.board
            if cfg.controller_id:
                device.controller_id = cfg.controller_id
            if cfg.ip:
                device.lan_url = f"http://{cfg.ip}"
        device.last_seen_at = _now()
        await db.commit()
        if result.rowcount:  # type: ignore[attr-defined]
            await self._emit(device, "device.config", {"deviceId": str(device.id), "rev": cfg.rev})

    # --- command results ------------------------------------------------------------------------

    async def _d_cmd_result(self, db: AsyncSession, device: Device, payload: bytes) -> None:
        res = protocol.CommandResult.model_validate_json(payload)
        _same_device(res.device, device)
        new = CommandStatus(res.status)
        allowed_from = (
            [CommandStatus.queued, CommandStatus.sent]
            if new == CommandStatus.received
            else [CommandStatus.queued, CommandStatus.sent, CommandStatus.received]
        )
        values: dict[str, Any] = {"status": new, "code": res.code, "message": res.message}
        if new in TERMINAL:
            values["completed_at"] = _now()
        row = (
            await db.execute(
                update(Command)
                .where(Command.id == res.id, Command.device_id == device.id, Command.status.in_(allowed_from))
                .values(**values)
                .returning(Command)
            )
        ).scalar_one_or_none()
        await db.commit()
        if row is None:
            stats["result_ignored"] += 1  # unknown, another device's, duplicate, or already final
            return
        await self._emit(device, "command.updated", command_event(row))

    # --- events, telemetry, gateway status ------------------------------------------------------

    async def _d_events(self, db: AsyncSession, device: Device, payload: bytes) -> None:
        body = _json_object(payload)
        db.add(DeviceEvent(device_id=device.id, payload=body))
        await db.commit()
        await self._emit(device, "device.event", {"deviceId": str(device.id), "event": body})

    async def _d_telemetry(self, db: AsyncSession, device: Device, payload: bytes) -> None:
        body = _json_object(payload)
        stmt = insert(DeviceState).values(device_id=device.id, telemetry=body)
        await db.execute(stmt.on_conflict_do_update(index_elements=[DeviceState.device_id], set_={"telemetry": body}))
        device.last_seen_at = _now()
        await db.commit()

    async def _g_status(self, db: AsyncSession, device: Device, payload: bytes) -> None:
        body = _json_object(payload)
        stmt = insert(DeviceState).values(device_id=device.id, reported=body)
        await db.execute(stmt.on_conflict_do_update(index_elements=[DeviceState.device_id], set_={"reported": body}))
        device.last_seen_at = _now()
        await db.commit()

    async def _emit(self, device: Device, event: str, data: dict[str, Any]) -> None:
        await publish_event(self.redis, device.home_id, event, data)


RETRIES = 3


def _retryable(exc: DBAPIError) -> bool:
    """Deadlock (40P01) or serialisation failure (40001): Postgres aborted us; try again."""
    code = getattr(exc.orig, "sqlstate", None) or getattr(getattr(exc.orig, "__cause__", None), "sqlstate", None)
    return code in ("40P01", "40001") or "deadlock detected" in str(exc)


def command_event(c: Command) -> dict[str, Any]:
    return {
        "commandId": str(c.id),
        "deviceId": str(c.device_id),
        "status": c.status.value,
        "code": c.code,
        "message": c.message,
    }


def _availability(payload: bytes) -> bool:
    text = payload.decode().strip()
    if text not in ("online", "offline"):
        raise ValueError("availability must be online or offline")
    return text == "online"


def _same_device(claimed: uuid.UUID, device: Device) -> None:
    if claimed != device.id:
        raise ValueError("payload device does not match the topic")


def _json_object(payload: bytes) -> dict[str, Any]:
    body = json.loads(payload)
    if not isinstance(body, dict):
        raise ValueError("expected a JSON object")
    return body
