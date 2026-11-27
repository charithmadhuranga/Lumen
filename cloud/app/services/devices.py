"""Claiming, registration and release of gateways and controllers (PROTOCOL.md §8).

Controllers are registered only by their gateway (DECISIONS D-17). Re-pairing
a device already known in the home (same MAC) reuses its row: a gateway gets
a fresh credential, a controller a fresh K_local, a new route epoch and a
higher ticket epoch (every older ticket stops working).
"""

from __future__ import annotations

import hashlib
import hmac
import re
import time
import uuid
from datetime import UTC, datetime, timedelta

import structlog
from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.core.security import new_opaque_token, token_digest
from app.core.settings import Settings
from app.db.base import uuid7
from app.models.account import User
from app.models.device import Claim, Device, DeviceKind, GatewayCredential
from app.models.home import Role
from app.services import keys, tickets
from app.services.broker import EmqxAdmin

log = structlog.get_logger("lumen.devices")

SETUP_CODE = re.compile(r"^[0-9A-HJKMNP-TV-Z]{8}$")  # Crockford base32, no I L O U
MAX_DEVICES_PER_HOME = 64
TICKET_TTL = {"control": timedelta(days=7), "admin": timedelta(hours=12)}


def _now() -> datetime:
    return datetime.now(UTC)


def normalise_setup_code(code: str) -> str:
    return code.replace("-", "").replace(" ", "").upper()


def setup_proof_hash(code: str) -> bytes:
    """What the app sends (hex) when it creates a gateway claim."""
    return hashlib.sha256(normalise_setup_code(code).encode()).digest()


async def _count_devices(db: AsyncSession, home_id: uuid.UUID) -> int:
    return len((await db.execute(select(Device.id).where(Device.home_id == home_id))).all())


async def create_claim(
    db: AsyncSession,
    settings: Settings,
    user: User,
    home_id: uuid.UUID,
    kind: DeviceKind,
    gateway_id: uuid.UUID | None = None,
    proof_hash: bytes | None = None,
) -> tuple[str, datetime]:
    if await _count_devices(db, home_id) >= MAX_DEVICES_PER_HOME:
        raise ApiError(409, "CAPACITY", f"A home can have at most {MAX_DEVICES_PER_HOME} devices")
    if kind == DeviceKind.controller:
        gw = await db.get(Device, gateway_id)
        if gw is None or gw.home_id != home_id or gw.kind != DeviceKind.gateway:
            raise ApiError(404, "NOT_FOUND", "Gateway not found in this home")
    token, digest = new_opaque_token("lct_")
    expires = _now() + timedelta(seconds=settings.claim_ttl_s)
    db.add(
        Claim(
            token_hash=digest,
            kind=kind,
            user_id=user.id,
            home_id=home_id,
            gateway_id=gateway_id,
            setup_proof_hash=proof_hash,
            expires_at=expires,
        )
    )
    await db.commit()
    return token, expires


async def _redeem(db: AsyncSession, token: str, kind: DeviceKind) -> Claim:
    """Mark the claim used, atomically; exactly one caller can redeem it."""
    claim = (
        await db.execute(
            update(Claim)
            .where(
                Claim.token_hash == token_digest(token),
                Claim.kind == kind,
                Claim.used_at.is_(None),
                Claim.expires_at > _now(),
            )
            .values(used_at=_now())
            .returning(Claim)
        )
    ).scalar_one_or_none()
    if claim is None:
        raise ApiError(400, "INVALID_CLAIM", "The claim code is invalid, used or expired")
    return claim


async def register_gateway(
    db: AsyncSession,
    settings: Settings,
    token: str,
    setup_code: str,
    hw: dict[str, str | None],
    version: str,
) -> tuple[Device, str]:
    claim = await _redeem(db, token, DeviceKind.gateway)
    code = normalise_setup_code(setup_code)
    assert claim.setup_proof_hash is not None  # guaranteed by the table constraint
    if not SETUP_CODE.match(code) or not hmac.compare_digest(setup_proof_hash(code), claim.setup_proof_hash):
        # The claim stays used: a wrong code burns it, so guessing needs a new claim each time.
        await db.commit()
        raise ApiError(400, "INVALID_SETUP_CODE", "The setup code does not match this gateway")
    gateway = (
        await db.execute(
            select(Device).where(
                Device.home_id == claim.home_id, Device.kind == DeviceKind.gateway, Device.mac == hw["mac"]
            )
        )
    ).scalar_one_or_none()
    if gateway is None:
        gateway = Device(
            kind=DeviceKind.gateway,
            home_id=claim.home_id,
            name="Lumen Gateway",
            mac=str(hw["mac"]),
            hw_model=str(hw["model"]),
            fw_version=version,
        )
        db.add(gateway)
    gateway.hw_model, gateway.board, gateway.chip, gateway.fw_version = (
        str(hw["model"]),
        hw.get("board"),
        hw.get("chip"),
        version,
    )
    await db.flush()
    # Any earlier credential of this gateway stops working now.
    await db.execute(
        update(GatewayCredential)
        .where(GatewayCredential.gateway_id == gateway.id, GatewayCredential.revoked_at.is_(None))
        .values(revoked_at=_now())
    )
    secret, digest = new_opaque_token("")
    db.add(GatewayCredential(gateway_id=gateway.id, secret_hash=digest))
    claim.device_id = gateway.id
    await db.commit()
    log.info("gateway_registered", gateway_id=str(gateway.id), home_id=str(gateway.home_id))
    return gateway, secret


async def authenticate_gateway(db: AsyncSession, gateway_id: uuid.UUID, secret: str) -> Device | None:
    row = (
        await db.execute(
            select(Device)
            .join(GatewayCredential, GatewayCredential.gateway_id == Device.id)
            .where(
                Device.id == gateway_id,
                Device.kind == DeviceKind.gateway,
                GatewayCredential.secret_hash == token_digest(secret),
                GatewayCredential.revoked_at.is_(None),
            )
        )
    ).scalar_one_or_none()
    return row


async def register_controller(
    db: AsyncSession, settings: Settings, gateway: Device, body: dict[str, object]
) -> tuple[Device, bytes]:
    claim = await _redeem(db, str(body["claim_token"]), DeviceKind.controller)
    if claim.gateway_id != gateway.id or claim.home_id != gateway.home_id:
        await db.rollback()  # not this gateway's claim: leave it unused for the right one
        raise ApiError(403, "FORBIDDEN", "This claim was issued for another gateway")
    hw: dict[str, str | None] = body["hw"]  # type: ignore[assignment]
    device = (
        await db.execute(
            select(Device).where(
                Device.home_id == gateway.home_id, Device.kind == DeviceKind.controller, Device.mac == hw["mac"]
            )
        )
    ).scalar_one_or_none()
    local_key = keys.new_local_key()
    if device is None:
        device = Device(
            id=uuid7(),  # needed before flush: the id is the key wrap's associated data
            kind=DeviceKind.controller,
            home_id=gateway.home_id,
            name=str(body["controller_id"]),
            mac=str(hw["mac"]),
            hw_model=str(hw["model"]),
            fw_version=str(body["fw"]),
            route_epoch=0,
            ticket_epoch=0,
        )
        db.add(device)
    else:
        device.ticket_epoch += 1  # tickets minted with the old key/epoch die
    device.route_epoch += 1
    device.gateway_id = gateway.id
    device.controller_id = str(body["controller_id"])
    device.lan_url = str(body["url"])
    device.hw_model, device.board, device.chip, device.fw_version = (
        str(hw["model"]),
        hw.get("board"),
        hw.get("chip"),
        str(body["fw"]),
    )
    device.local_key_enc, device.local_key_kid = keys.wrap(settings, device.id, local_key)
    await db.flush()
    claim.device_id = device.id
    await db.commit()
    log.info("controller_registered", device_id=str(device.id), gateway_id=str(gateway.id))
    return device, local_key


def local_key_of(settings: Settings, device: Device) -> bytes:
    if device.local_key_enc is None or device.local_key_kid is None:
        raise ApiError(409, "CONFLICT", "This device has no local key")
    return keys.unwrap(settings, device.id, device.local_key_enc, device.local_key_kid)


async def children_of(db: AsyncSession, gateway_id: uuid.UUID) -> list[Device]:
    return list(
        (
            await db.scalars(
                select(Device)
                .where(Device.gateway_id == gateway_id, Device.kind == DeviceKind.controller)
                .order_by(Device.created_at)
            )
        ).all()
    )


async def release(db: AsyncSession, settings: Settings, device: Device) -> None:
    """Remove a device from the cloud. A gateway's credential dies with it and
    its controllers become unassigned (they keep working locally)."""
    kick = f"g:{device.id}" if device.kind == DeviceKind.gateway else None
    if device.kind == DeviceKind.gateway:
        await db.execute(
            update(Device)
            .where(Device.gateway_id == device.id)
            .values(gateway_id=None, route_epoch=Device.route_epoch + 1)
        )
    await db.delete(device)
    await db.commit()
    if kick:
        await EmqxAdmin(settings).kick(kick)


def mint_ticket(settings: Settings, device: Device, user: User, role: Role, scope: str) -> tuple[str, datetime]:
    if device.kind != DeviceKind.controller:
        raise ApiError(400, "INVALID_REQUEST", "Local tickets are for controllers")
    if scope == "admin" and role not in (Role.owner, Role.admin):
        raise ApiError(403, "FORBIDDEN", "Only owners and admins can get admin access")
    now = int(time.time())
    exp = now + int(TICKET_TTL[scope].total_seconds())
    claims = tickets.Claims(sub=f"u:{user.id}", dev=str(device.id), scp=scope, iat=now, exp=exp, ep=device.ticket_epoch)
    return tickets.mint(local_key_of(settings, device), claims), datetime.fromtimestamp(exp, UTC)
