"""The cloud's MQTT connection (one per API instance), client id `svc:api-<random>`.

- MQTT 5, TLS, clean start, session expiry 0: a restart never inherits old messages.
- Shared subscriptions (`$share/lumen/...`), so N instances split the device
  traffic and each message is ingested once.
- The receive loop only enqueues (bounded queue; overflow is counted and the
  message dropped, the next state report supersedes it); WORKERS tasks ingest.
- Reconnects with backoff (0.5 s doubling to 30 s, ±20 % jitter).
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import random
import secrets
import ssl
from collections.abc import Awaitable, Callable
from typing import Any
from urllib.parse import urlparse

import aiomqtt
import structlog
from paho.mqtt.packettypes import PacketTypes
from paho.mqtt.properties import Properties

from app.core.settings import Settings

log = structlog.get_logger("lumen.mqtt")

QUEUE_SIZE = 2000
WORKERS = 4
MAX_PAYLOAD = 16 * 1024
SUBSCRIPTIONS = [
    "$share/lumen/lumen/v1/d/+/availability",
    "$share/lumen/lumen/v1/d/+/state",
    "$share/lumen/lumen/v1/d/+/cmd/result",
    "$share/lumen/lumen/v1/d/+/config/reported",
    "$share/lumen/lumen/v1/d/+/events",
    "$share/lumen/lumen/v1/d/+/telemetry",
    "$share/lumen/lumen/v1/g/+/availability",
    "$share/lumen/lumen/v1/g/+/status",
]

Handler = Callable[[str, bytes, bool], Awaitable[None]]  # (topic, payload, retained)


class BridgeUnavailable(Exception):
    pass


class Publisher:
    """What the rest of the app needs from the bridge (a fake one in unit tests)."""

    connected: bool = False

    async def publish(
        self, topic: str, payload: dict[str, Any] | bytes, *, retain: bool = False, expiry_s: int | None = None
    ) -> None:
        # The base publisher is "no broker" (bridge disabled): behaves like a disconnected bridge.
        raise BridgeUnavailable("MQTT bridge disabled")


class MqttBridge(Publisher):
    def __init__(self, settings: Settings, handler: Handler) -> None:
        url = urlparse(settings.broker_internal_url)
        if url.scheme != "mqtts" or not url.hostname:
            raise ValueError("broker_internal_url must be mqtts://host:port")
        self.host, self.port = url.hostname, url.port or 8883
        self.client_id = f"svc:api-{secrets.token_hex(4)}"
        self.password = settings.mqtt_service_password.get_secret_value()
        self.tls = ssl.create_default_context(cafile=settings.broker_ca_file or None)
        self.handler = handler
        self.queue: asyncio.Queue[tuple[str, bytes, bool]] = asyncio.Queue(QUEUE_SIZE)
        self.client: aiomqtt.Client | None = None
        self.connected = False
        self.received = self.dropped = self.failed = 0
        self._tasks: list[asyncio.Task[None]] = []

    async def start(self) -> None:
        self._tasks = [asyncio.create_task(self._connection_loop(), name="mqtt-bridge")]
        self._tasks += [asyncio.create_task(self._worker(), name=f"mqtt-worker-{i}") for i in range(WORKERS)]

    async def stop(self) -> None:
        for task in self._tasks:
            task.cancel()
        for task in self._tasks:
            with contextlib.suppress(asyncio.CancelledError):
                await task

    async def publish(
        self, topic: str, payload: dict[str, Any] | bytes, *, retain: bool = False, expiry_s: int | None = None
    ) -> None:
        client = self.client
        if client is None or not self.connected:
            raise BridgeUnavailable("broker not connected")
        data = payload if isinstance(payload, bytes) else json.dumps(payload, separators=(",", ":")).encode()
        props = None
        if expiry_s is not None:
            props = Properties(PacketTypes.PUBLISH)  # type: ignore[no-untyped-call]
            props.MessageExpiryInterval = max(1, expiry_s)
        try:
            await client.publish(topic, data, qos=1, retain=retain, properties=props, timeout=5)
        except aiomqtt.MqttError as exc:
            raise BridgeUnavailable(str(exc)) from exc

    async def _connection_loop(self) -> None:
        backoff = 0.5
        while True:
            try:
                async with aiomqtt.Client(
                    self.host,
                    self.port,
                    identifier=self.client_id,
                    username=self.client_id,
                    password=self.password,
                    tls_context=self.tls,
                    protocol=aiomqtt.ProtocolVersion.V5,
                    clean_start=True,
                    keepalive=30,
                    timeout=10,
                ) as client:
                    for topic in SUBSCRIPTIONS:
                        await client.subscribe(topic, qos=1)
                    self.client, self.connected, backoff = client, True, 0.5
                    log.info("mqtt_connected", client_id=self.client_id)
                    async for message in client.messages:
                        self.received += 1
                        payload = message.payload if isinstance(message.payload, bytes) else b""
                        if len(payload) > MAX_PAYLOAD:
                            self.dropped += 1
                            continue
                        try:
                            self.queue.put_nowait((str(message.topic), payload, bool(message.retain)))
                        except asyncio.QueueFull:
                            self.dropped += 1
            except asyncio.CancelledError:
                raise
            except aiomqtt.MqttError as exc:
                log.warning("mqtt_disconnected", error=str(exc)[:120], retry_s=round(backoff, 1))
            finally:
                self.connected, self.client = False, None
            await asyncio.sleep(backoff * random.uniform(0.8, 1.2))  # noqa: S311 - jitter, not crypto
            backoff = min(backoff * 2, 30.0)

    async def _worker(self) -> None:
        while True:
            topic, payload, retained = await self.queue.get()
            try:
                await self.handler(topic, payload, retained)
            except asyncio.CancelledError:
                raise
            except Exception:
                self.failed += 1
                log.exception("ingest_failed", topic=topic)
            finally:
                self.queue.task_done()
