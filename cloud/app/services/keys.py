"""Per-controller local keys (K_local) wrapped under the cloud's KEK.

K_local is 32 random bytes. The cloud must be able to read it back (to mint
tickets for app members), so it is encrypted, not hashed: AES-256-GCM with a
key derived from LUMEN_KEK by HKDF-SHA256, a random 96-bit nonce, and the
device id as associated data, so a ciphertext copied onto another device row
does not decrypt. `kid` names the KEK generation for rotation (P11).
"""

from __future__ import annotations

import base64
import hashlib
import os
import uuid

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

from app.core.settings import Settings

KEY_LEN = 32


def _kek(settings: Settings) -> tuple[bytes, str]:
    secret = settings.kek.get_secret_value().encode()
    key = HKDF(algorithm=hashes.SHA256(), length=32, salt=b"lumen-kek-v1", info=b"local-key-wrap").derive(secret)
    kid = hashlib.sha256(b"lumen-kek-kid:" + secret).hexdigest()[:12]
    return key, kid


def new_local_key() -> bytes:
    return os.urandom(KEY_LEN)


def wrap(settings: Settings, device_id: uuid.UUID, local_key: bytes) -> tuple[bytes, str]:
    key, kid = _kek(settings)
    nonce = os.urandom(12)
    return nonce + AESGCM(key).encrypt(nonce, local_key, device_id.bytes), kid


def unwrap(settings: Settings, device_id: uuid.UUID, blob: bytes, kid: str) -> bytes:
    key, current_kid = _kek(settings)
    if kid != current_kid:
        raise ValueError("local key was wrapped under another KEK generation")
    return AESGCM(key).decrypt(blob[:12], blob[12:], device_id.bytes)


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()
