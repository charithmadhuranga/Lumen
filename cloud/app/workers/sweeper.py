"""Background housekeeping, one task per API instance.

- Every second: commands past their expiry without a final result become
  `expired` (UPDATE ... RETURNING, so with several instances each row is
  expired and announced exactly once).
- Every 10 minutes: device events and finished commands older than 30 days
  are deleted (bounded storage).
"""

from __future__ import annotations

import asyncio
import contextlib
from datetime import UTC, datetime, timedelta

import structlog
from redis.asyncio import Redis
from sqlalchemy import delete, update
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker

from app.models.messaging import TERMINAL, Command, CommandStatus, DeviceEvent
from app.services.events import publish_event
from app.services.ingest import command_event

log = structlog.get_logger("lumen.sweeper")

RETENTION = timedelta(days=30)
OPEN = [CommandStatus.queued, CommandStatus.sent, CommandStatus.received]


async def expire_commands(sessionmaker: async_sessionmaker[AsyncSession], redis: Redis) -> int:
    now = datetime.now(UTC)
    async with sessionmaker() as db:
        rows = (
            (
                await db.execute(
                    update(Command)
                    .where(Command.status.in_(OPEN), Command.expires_at < now)
                    .values(status=CommandStatus.expired, completed_at=now)
                    .returning(Command)
                )
            )
            .scalars()
            .all()
        )
        await db.commit()
    for row in rows:
        await publish_event(redis, row.home_id, "command.updated", command_event(row))
    return len(rows)


async def prune(sessionmaker: async_sessionmaker[AsyncSession]) -> None:
    cutoff = datetime.now(UTC) - RETENTION
    async with sessionmaker() as db:
        await db.execute(delete(DeviceEvent).where(DeviceEvent.received_at < cutoff))
        await db.execute(delete(Command).where(Command.status.in_(list(TERMINAL)), Command.created_at < cutoff))
        await db.commit()


class Sweeper:
    def __init__(self, sessionmaker: async_sessionmaker[AsyncSession], redis: Redis) -> None:
        self.sessionmaker, self.redis = sessionmaker, redis
        self._task: asyncio.Task[None] | None = None

    async def start(self) -> None:
        self._task = asyncio.create_task(self._run(), name="sweeper")

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._task

    async def _run(self) -> None:
        ticks = 0
        while True:
            try:
                await expire_commands(self.sessionmaker, self.redis)
                if ticks % 600 == 0:
                    await prune(self.sessionmaker)
            except asyncio.CancelledError:
                raise
            except Exception:
                log.exception("sweeper_failed")
            ticks += 1
            await asyncio.sleep(1.0)
