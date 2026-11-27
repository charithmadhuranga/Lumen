"""Passwords, access tokens and opaque tokens.

- Passwords: argon2id (RFC 9106 second recommended profile: 64 MiB, t=3, p=4).
- Access tokens: JWT HS256, 15 minutes, claims {sub, sid, iat, exp, typ, iss}.
  `kid` selects the key, so the secret can be rotated: LUMEN_JWT_SECRET signs,
  LUMEN_JWT_PREVIOUS_SECRET still verifies until the old tokens expire.
- Opaque tokens (refresh, email, claim): prefix + 43 base64url chars
  (32 random bytes). Only the SHA-256 digest is stored.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import secrets
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta

import jwt
from argon2 import PasswordHasher
from argon2.exceptions import InvalidHashError, VerificationError, VerifyMismatchError

from app.core.settings import Settings

ACCESS_TTL = timedelta(minutes=15)
ISSUER = "lumen-cloud"

_hasher = PasswordHasher(time_cost=3, memory_cost=64 * 1024, parallelism=4, hash_len=32, salt_len=16)
# Verified when the e-mail is unknown, so sign-in takes the same time either way.
_DUMMY_HASH = _hasher.hash(secrets.token_urlsafe(32))


def hash_password(password: str) -> str:
    return _hasher.hash(password)


def verify_password(password: str, password_hash: str | None) -> bool:
    try:
        return _hasher.verify(password_hash or _DUMMY_HASH, password) and password_hash is not None
    except (VerifyMismatchError, VerificationError, InvalidHashError):
        return False


def password_needs_rehash(password_hash: str) -> bool:
    return _hasher.check_needs_rehash(password_hash)


def new_opaque_token(prefix: str) -> tuple[str, bytes]:
    """Return (token for the client, SHA-256 digest to store)."""
    raw = secrets.token_bytes(32)
    token = prefix + base64.urlsafe_b64encode(raw).rstrip(b"=").decode()
    return token, token_digest(token)


def token_digest(token: str) -> bytes:
    return hashlib.sha256(token.encode()).digest()


def _kid(secret: str) -> str:
    # A short public fingerprint of the key (not the key itself).
    return hashlib.sha256(b"lumen-jwt-kid:" + secret.encode()).hexdigest()[:12]


@dataclass(frozen=True)
class AccessClaims:
    user_id: uuid.UUID
    session_id: uuid.UUID
    expires_at: datetime


class TokenError(Exception):
    pass


def issue_access_token(settings: Settings, user_id: uuid.UUID, session_id: uuid.UUID) -> tuple[str, datetime]:
    now = datetime.now(UTC)
    exp = now + ACCESS_TTL
    secret = settings.jwt_secret.get_secret_value()
    token = jwt.encode(
        {"sub": str(user_id), "sid": str(session_id), "iat": now, "exp": exp, "typ": "access", "iss": ISSUER},
        secret,
        algorithm="HS256",
        headers={"kid": _kid(secret)},
    )
    return token, exp


def decode_access_token(settings: Settings, token: str) -> AccessClaims:
    keys = {_kid(s): s for s in _secrets(settings)}
    try:
        kid = jwt.get_unverified_header(token).get("kid")
        secret = keys.get(kid) if isinstance(kid, str) else None
        if secret is None:
            raise TokenError("unknown key")
        data = jwt.decode(
            token,
            secret,
            algorithms=["HS256"],
            issuer=ISSUER,
            options={"require": ["sub", "sid", "iat", "exp", "typ", "iss"]},
        )
        if data["typ"] != "access":
            raise TokenError("wrong token type")
        return AccessClaims(uuid.UUID(data["sub"]), uuid.UUID(data["sid"]), datetime.fromtimestamp(data["exp"], tz=UTC))
    except (jwt.PyJWTError, ValueError, KeyError) as exc:
        raise TokenError("invalid token") from exc


def _secrets(settings: Settings) -> list[str]:
    out = [settings.jwt_secret.get_secret_value()]
    if settings.jwt_previous_secret is not None:
        out.append(settings.jwt_previous_secret.get_secret_value())
    return out


def constant_time_equal(a: bytes, b: bytes) -> bool:
    return hmac.compare_digest(a, b)
