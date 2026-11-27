"""Real-time events for apps (PROTOCOL.md §10).

Any API instance publishes an event to one Redis channel; every instance runs
one EventHub that subscribes once and hands each event to its own WebSocket
connections whose user belongs to the event's home. A connection whose bounded
queue fills up (slow client) is marked and closed by its handler.
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import time
import uuid
from dataclasses import dataclass, field
from typing import Any

import structlog
from redis.asyncio import Redis

log = structlog.get_logger("lumen.events")

CHANNEL = "lumen:events"
QUEUE_SIZE = 100


def event_message(event: str, data: dict[str, Any]) -> dict[str, Any]:
    return {"v": 1, "event": event, "ts": int(time.time() * 1000), "data": data}


async def publish_event(redis: Redis, home_id: uuid.UUID, event: str, data: dict[str, Any]) -> None:
    body = {"home": str(home_id), **event_message(event, data)}
    try:
        await redis.publish(CHANNEL, json.dumps(body, separators=(",", ":"), default=str))
    except Exception as exc:
        log.warning("event_publish_failed", event=event, error=type(exc).__name__)


@dataclass(eq=False)
class Subscriber:
    homes: set[str]
    queue: asyncio.Queue[dict[str, Any]] = field(default_factory=lambda: asyncio.Queue(QUEUE_SIZE))
    overflowed: bool = False


class EventHub:
    def __init__(self, redis: Redis) -> None:
        self.redis = redis
        self.subscribers: set[Subscriber] = set()
        self._task: asyncio.Task[None] | None = None
        self.delivered = 0
        self.dropped = 0

    async def start(self) -> None:
        self._task = asyncio.create_task(self._run(), name="event-hub")

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._task

    def add(self, homes: set[str]) -> Subscriber:
        sub = Subscriber(homes)
        self.subscribers.add(sub)
        return sub

    def remove(self, sub: Subscriber) -> None:
        self.subscribers.discard(sub)

    def dispatch(self, body: dict[str, Any]) -> None:
        home = body.pop("home", None)
        for sub in list(self.subscribers):
            if home not in sub.homes or sub.overflowed:
                continue
            try:
                sub.queue.put_nowait(body)
                self.delivered += 1
            except asyncio.QueueFull:
                sub.overflowed = True
                self.dropped += 1

    async def _run(self) -> None:
        backoff = 0.5
        while True:
            try:
                async with self.redis.pubsub() as pubsub:
                    await pubsub.subscribe(CHANNEL)
                    backoff = 0.5
                    async for message in pubsub.listen():
                        if message.get("type") != "message":
                            continue
                        try:
                            self.dispatch(json.loads(message["data"]))
                        except (ValueError, TypeError):
                            log.warning("bad_event_message")
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                log.warning("event_hub_reconnect", error=type(exc).__name__)
                await asyncio.sleep(backoff)
                backoff = min(backoff * 2, 10.0)
