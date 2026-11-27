"""E-mail sending behind an interface. P2 ships a development sender that logs
that a message was sent (never the token) and keeps the last messages in
memory for tests. The real provider (owner decision O-4) comes in P10.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from typing import Protocol

import structlog

log = structlog.get_logger("lumen.email")


@dataclass(frozen=True)
class EmailMessage:
    to: str
    kind: str  # "verify_email" | "reset_password"
    token: str  # goes into the link; never logged


class EmailSender(Protocol):
    async def send(self, message: EmailMessage) -> None: ...


class DevEmailSender:
    def __init__(self, keep: int = 50) -> None:
        self.outbox: deque[EmailMessage] = deque(maxlen=keep)

    async def send(self, message: EmailMessage) -> None:
        self.outbox.append(message)
        log.info("email_queued", kind=message.kind, to_domain=message.to.rsplit("@", 1)[-1])
