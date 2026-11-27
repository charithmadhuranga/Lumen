"""Broker authorisation rules (PROTOCOL.md §2, §7) and the EMQX admin client.

The rules are pure functions over (client, topic, action, assigned children),
so they can be tested exhaustively without a broker.
"""

from __future__ import annotations

import re
import uuid
from collections.abc import Collection
from dataclasses import dataclass
from typing import Literal

import httpx
import structlog

from app.core.settings import Settings

log = structlog.get_logger("lumen.broker")

PREFIX = "lumen/v1/"
_UUID = r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"
_DEVICE_TOPIC = re.compile(
    rf"^lumen/v1/d/({_UUID})/(availability|state|cmd|cmd/result|config/reported|events|telemetry)$"
)
_GATEWAY_TOPIC = re.compile(rf"^lumen/v1/g/({_UUID})/(availability|status|children)$")

# What a gateway may do on its children's device topics and its own gateway topics.
_GATEWAY_DEVICE_PUB = {"availability", "state", "cmd/result", "config/reported", "events", "telemetry"}
_GATEWAY_DEVICE_SUB = {"cmd"}
_GATEWAY_OWN_PUB = {"availability", "status"}
_GATEWAY_OWN_SUB = {"children"}

Action = Literal["publish", "subscribe"]


@dataclass(frozen=True)
class Client:
    kind: Literal["gateway", "service"]
    gateway_id: uuid.UUID | None = None


def parse_client(clientid: str, username: str) -> Client | None:
    """`g:{uuid}` gateways and `svc:{name}` service clients; username must equal clientid."""
    if clientid != username or len(clientid) > 64:
        return None
    if clientid.startswith("g:"):
        try:
            gid = uuid.UUID(clientid[2:])
        except ValueError:
            return None
        return Client("gateway", gid) if str(gid) == clientid[2:] else None
    if re.fullmatch(r"svc:[a-z0-9-]{1,32}", clientid):
        return Client("service")
    return None


def gateway_allowed(gateway_id: uuid.UUID, children: Collection[uuid.UUID], topic: str, action: Action) -> bool:
    if "+" in topic or "#" in topic or topic.startswith("$"):
        return False  # gateways subscribe to exact topics only
    if m := _GATEWAY_TOPIC.match(topic):
        if uuid.UUID(m.group(1)) != gateway_id:
            return False
        return m.group(2) in (_GATEWAY_OWN_PUB if action == "publish" else _GATEWAY_OWN_SUB)
    if m := _DEVICE_TOPIC.match(topic):
        if uuid.UUID(m.group(1)) not in children:
            return False
        return m.group(2) in (_GATEWAY_DEVICE_PUB if action == "publish" else _GATEWAY_DEVICE_SUB)
    return False


def service_allowed(topic: str, action: Action) -> bool:
    """The cloud bridge: publishes commands and assignments, subscribes to the rest (wildcards allowed)."""
    if action == "publish":
        return bool(
            re.fullmatch(rf"lumen/v1/d/{_UUID}/cmd", topic) or re.fullmatch(rf"lumen/v1/g/{_UUID}/children", topic)
        )
    # Shared subscriptions arrive as $share/<group>/<filter>.
    if topic.startswith("$share/"):
        parts = topic.split("/", 2)
        if len(parts) != 3:
            return False
        topic = parts[2]
    return topic.startswith(PREFIX)


def wanted_topic_ids(topic: str) -> uuid.UUID | None:
    """Device id in a device topic (for loading only what the check needs)."""
    m = _DEVICE_TOPIC.match(topic)
    return uuid.UUID(m.group(1)) if m else None


class EmqxAdmin:
    """Kicks clients through the EMQX management API (best effort: the auth
    hook already denies a revoked credential on its next connect)."""

    def __init__(self, settings: Settings) -> None:
        self.base = settings.emqx_api_url.rstrip("/")
        self.auth = (settings.emqx_api_key, settings.emqx_api_secret.get_secret_value())

    async def kick(self, clientid: str) -> bool:
        if not self.base:
            return False
        try:
            async with httpx.AsyncClient(timeout=3.0) as client:
                r = await client.delete(f"{self.base}/api/v5/clients/{clientid}", auth=self.auth)
            if r.status_code in (204, 404):
                return True
            log.warning("emqx_kick_failed", clientid=clientid, status=r.status_code)
        except httpx.HTTPError as exc:
            log.warning("emqx_kick_failed", clientid=clientid, error=type(exc).__name__)
        return False
