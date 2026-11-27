"""Local tickets (PROTOCOL.md §6): the cloud's implementation.

Must produce byte-identical tickets to protocol/tools/ticket_ref.py and the Go
gateway, and give the same verdict and reason when verifying
(tests/test_tickets.py runs protocol/fixtures/tickets.json).
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import re
from dataclasses import dataclass

MAX_LENGTH = 512
LIFETIME = {"control": 7 * 24 * 3600, "admin": 12 * 3600}
CLOCK_SKEW_S = 300
_B64 = re.compile(r"^[A-Za-z0-9_-]+$")
_UUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
_KEYS = ("sub", "dev", "scp", "iat", "exp", "ep")


class TicketError(Exception):
    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


@dataclass(frozen=True)
class Claims:
    sub: str
    dev: str
    scp: str
    iat: int
    exp: int
    ep: int


def _b64e(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def _b64d(text: str) -> bytes:
    if not _B64.match(text):
        raise TicketError("format")
    try:
        return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))
    except ValueError:
        raise TicketError("format") from None


def _mac(key: bytes, body: str) -> bytes:
    return hmac.new(key, body.encode(), hashlib.sha256).digest()


def mint(key: bytes, claims: Claims) -> str:
    if claims.scp not in LIFETIME or not 0 < claims.exp - claims.iat <= LIFETIME[claims.scp]:
        raise ValueError("ticket lifetime or scope outside the protocol limits")
    payload = json.dumps({k: getattr(claims, k) for k in _KEYS}, separators=(",", ":"))
    body = "LT1." + _b64e(payload.encode())
    return body + "." + _b64e(_mac(key, body))


def verify(ticket: str, key: bytes, device: str, epoch: int, now: int) -> Claims:
    if len(ticket) > MAX_LENGTH:
        raise TicketError("format")
    parts = ticket.split(".")
    if len(parts) != 3 or parts[0] != "LT1":
        raise TicketError("format")
    raw, mac = _b64d(parts[1]), _b64d(parts[2])
    if not hmac.compare_digest(mac, _mac(key, parts[0] + "." + parts[1])):
        raise TicketError("mac")
    try:
        c = json.loads(raw)
    except ValueError:
        raise TicketError("claims") from None
    if not isinstance(c, dict) or set(c) != set(_KEYS):
        raise TicketError("claims")
    ints_ok = all(isinstance(c[k], int) and not isinstance(c[k], bool) and c[k] >= 0 for k in ("iat", "exp", "ep"))
    sub, dev = c["sub"], c["dev"]
    if (
        not ints_ok
        or c["scp"] not in LIFETIME
        or not isinstance(sub, str)
        or not isinstance(dev, str)
        or sub[:2] not in ("u:", "g:")
        or not _UUID.match(sub[2:])
        or not _UUID.match(dev)
    ):
        raise TicketError("claims")
    if dev != device:
        raise TicketError("device")
    if c["ep"] != epoch:
        raise TicketError("epoch")
    if c["exp"] <= c["iat"] or c["exp"] - c["iat"] > LIFETIME[c["scp"]]:
        raise TicketError("lifetime")
    if c["iat"] > now + CLOCK_SKEW_S:
        raise TicketError("not_yet_valid")
    if c["exp"] <= now:
        raise TicketError("expired")
    return Claims(**{k: c[k] for k in _KEYS})
