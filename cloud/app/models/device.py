"""Devices (gateways and controllers), gateway broker credentials, claims.

DECISIONS D-17: controllers never connect to the broker. Only gateways hold a
credential; a controller row records which gateway represents it.
"""

from __future__ import annotations

import enum
import uuid
from datetime import datetime

from sqlalchemy import CheckConstraint, DateTime, Enum, ForeignKey, Index, Integer, LargeBinary, String, func
from sqlalchemy.dialects.postgresql import UUID
from sqlalchemy.orm import Mapped, mapped_column

from app.db.base import Base, IdMixin, TimestampMixin


class DeviceKind(enum.StrEnum):
    gateway = "gateway"
    controller = "controller"


def _enum(e: type[enum.StrEnum], name: str) -> Enum:
    return Enum(e, name=name, values_callable=lambda x: [m.value for m in x])


class Device(IdMixin, TimestampMixin, Base):
    __tablename__ = "devices"

    kind: Mapped[DeviceKind] = mapped_column(_enum(DeviceKind, "device_kind"), nullable=False)
    home_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("homes.id", ondelete="CASCADE"), nullable=False, index=True
    )
    room_id: Mapped[uuid.UUID | None] = mapped_column(UUID(as_uuid=True), ForeignKey("rooms.id", ondelete="SET NULL"))
    name: Mapped[str] = mapped_column(String(64), nullable=False)
    hw_model: Mapped[str] = mapped_column(String(48), nullable=False)
    board: Mapped[str | None] = mapped_column(String(48))
    mac: Mapped[str] = mapped_column(String(17), nullable=False)
    chip: Mapped[str | None] = mapped_column(String(32))
    fw_version: Mapped[str] = mapped_column(String(32), nullable=False)
    # Controllers only: local name (lumen-0798), LAN URL, representing gateway.
    controller_id: Mapped[str | None] = mapped_column(String(32))
    lan_url: Mapped[str | None] = mapped_column(String(64))
    gateway_id: Mapped[uuid.UUID | None] = mapped_column(
        UUID(as_uuid=True), ForeignKey("devices.id", ondelete="SET NULL"), index=True
    )
    # Raised whenever the controller is (re)assigned; the broker ACL and the
    # gateway's children list carry it (PROTOCOL §2).
    route_epoch: Mapped[int] = mapped_column(Integer, nullable=False, default=0, server_default="0")
    # Local tickets carry it; raising it revokes every older ticket (PROTOCOL §6).
    ticket_epoch: Mapped[int] = mapped_column(Integer, nullable=False, default=0, server_default="0")
    # K_local, AES-256-GCM under the KEK (app/services/keys.py): nonce || ciphertext+tag.
    local_key_enc: Mapped[bytes | None] = mapped_column(LargeBinary(64))
    local_key_kid: Mapped[str | None] = mapped_column(String(16))
    last_seen_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        CheckConstraint(
            "(kind = 'gateway' AND gateway_id IS NULL AND controller_id IS NULL)"
            " OR (kind = 'controller' AND local_key_enc IS NOT NULL)",
            name="kind_fields",
        ),
        CheckConstraint("route_epoch >= 0 AND ticket_epoch >= 0", name="epochs"),
        Index("ix_devices_home_mac", "home_id", "mac"),
    )


class GatewayCredential(IdMixin, Base):
    """Broker/REST secret of a gateway (256-bit random; only SHA-256 stored)."""

    __tablename__ = "gateway_credentials"

    gateway_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE"), nullable=False, index=True
    )
    secret_hash: Mapped[bytes] = mapped_column(LargeBinary(32), nullable=False, unique=True)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, server_default=func.now())
    revoked_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))


class Claim(IdMixin, Base):
    """Single-use claim token issued to a signed-in user (PROTOCOL §8)."""

    __tablename__ = "claims"

    token_hash: Mapped[bytes] = mapped_column(LargeBinary(32), nullable=False, unique=True)
    kind: Mapped[DeviceKind] = mapped_column(_enum(DeviceKind, "device_kind"), nullable=False)
    user_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("users.id", ondelete="CASCADE"), nullable=False
    )
    home_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("homes.id", ondelete="CASCADE"), nullable=False, index=True
    )
    # Controller claims: the gateway that must redeem it.
    gateway_id: Mapped[uuid.UUID | None] = mapped_column(
        UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE")
    )
    # Gateway claims: SHA-256 of the normalised setup code shown on the gateway.
    setup_proof_hash: Mapped[bytes | None] = mapped_column(LargeBinary(32))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, server_default=func.now())
    expires_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    used_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    device_id: Mapped[uuid.UUID | None] = mapped_column(
        UUID(as_uuid=True), ForeignKey("devices.id", ondelete="SET NULL")
    )

    __table_args__ = (
        CheckConstraint(
            "(kind = 'gateway' AND setup_proof_hash IS NOT NULL AND gateway_id IS NULL)"
            " OR (kind = 'controller' AND gateway_id IS NOT NULL AND setup_proof_hash IS NULL)",
            name="kind_fields",
        ),
    )
