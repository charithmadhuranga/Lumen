#!/usr/bin/env python3
"""Reference verifier for Lumen local tickets (PROTOCOL.md §6).

    python3 protocol/tools/ticket_ref.py      # checks protocol/fixtures/tickets.json

The firmware (C++), gateway (Go) and cloud (Python) verifiers must give the
same verdict as this one for every vector, including the failure reason.
"""
import base64
import hashlib
import hmac
import json
import os
import re
import sys

MAX_LEN = 512
LIFETIME = {"control": 604800, "admin": 43200}
SKEW = 300
B64 = re.compile(r"^[A-Za-z0-9_-]+$")
UUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
KEYS = {"sub", "dev", "scp", "iat", "exp", "ep"}


class TicketError(Exception):
    def __init__(self, reason):
        super().__init__(reason)
        self.reason = reason


def _b64d(s):
    if not B64.match(s):
        raise TicketError("format")
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def verify(ticket, key, device, epoch, now):
    """Return the claims dict, or raise TicketError(reason).

    Check order (fixed, so every implementation reports the same reason):
    format -> mac -> claims -> device -> epoch -> lifetime -> not_yet_valid -> expired.
    """
    if len(ticket) > MAX_LEN:
        raise TicketError("format")
    parts = ticket.split(".")
    if len(parts) != 3 or parts[0] != "LT1":
        raise TicketError("format")
    body = parts[0] + "." + parts[1]
    try:
        raw = _b64d(parts[1])
        mac = _b64d(parts[2])
    except (ValueError, TicketError):
        raise TicketError("format")
    good = hmac.new(key, body.encode(), hashlib.sha256).digest()
    if not hmac.compare_digest(mac, good):
        raise TicketError("mac")
    try:
        c = json.loads(raw)
    except ValueError:
        raise TicketError("claims")
    if not isinstance(c, dict) or set(c) != KEYS:
        raise TicketError("claims")
    if c["scp"] not in LIFETIME or not isinstance(c["sub"], str) or not re.match(r"^[ug]:", c["sub"]):
        raise TicketError("claims")
    for k in ("iat", "exp", "ep"):
        if not isinstance(c[k], int) or isinstance(c[k], bool) or c[k] < 0:
            raise TicketError("claims")
    if not isinstance(c["dev"], str) or not UUID.match(c["dev"]) or not UUID.match(c["sub"][2:]):
        raise TicketError("claims")
    if c["dev"] != device:
        raise TicketError("device")
    if c["ep"] != epoch:
        raise TicketError("epoch")
    if c["exp"] <= c["iat"] or c["exp"] - c["iat"] > LIFETIME[c["scp"]]:
        raise TicketError("lifetime")
    if c["iat"] > now + SKEW:
        raise TicketError("not_yet_valid")
    if c["exp"] <= now:
        raise TicketError("expired")
    return c


def main():
    path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fixtures", "tickets.json")
    v = json.load(open(path, encoding="utf-8"))
    key = bytes.fromhex(v["key"])
    bad = 0
    for case in v["valid"]:
        try:
            got = verify(case["ticket"], key, v["device"], v["epoch"], v["now"])
            if got != case["claims"]:
                print(f"FAIL valid/{case['name']}: claims differ"); bad += 1
        except TicketError as e:
            print(f"FAIL valid/{case['name']}: rejected ({e.reason})"); bad += 1
    for case in v["invalid"]:
        try:
            verify(case["ticket"], key, v["device"], v["epoch"], v["now"])
            print(f"FAIL invalid/{case['name']}: accepted"); bad += 1
        except TicketError as e:
            if e.reason != case["reason"]:
                print(f"FAIL invalid/{case['name']}: reason {e.reason}, expected {case['reason']}"); bad += 1
    print(f"tickets: {len(v['valid'])} valid, {len(v['invalid'])} invalid vectors, {bad} failure(s)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
