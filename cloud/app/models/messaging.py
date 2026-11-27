"""Reported state, reported configuration, commands and device events (P4)."""

from __future__ import annotations

import enum
import uuid
from datetime import datetime
from typing import Any

from sqlalchemy import BigInteger, Boolean, DateTime, Enum, ForeignKey, Index, Integer, String, UniqueConstraint, func
from sqlalchemy.dialects.postgresql import JSONB, UUID
from sqlalchemy.orm import Mapped, mapped_column

from app.db.base import Base


class DeviceState(Base):
    """Latest reported state and availability of a device (gateway or controller)."""

    __tablename__ = "device_states"

    device_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE"), primary_key=True
    )
    online: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False, server_default="false")
    reported: Mapped[dict[str, Any] | None] = mapped_column(JSONB)
    # Device timestamp (ms) of `reported`; only a newer report replaces it.
    reported_ts: Mapped[int | None] = mapped_column(BigInteger)
    telemetry: Mapped[dict[str, Any] | None] = mapped_column(JSONB)
    availability_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, server_default=func.now(), onupdate=func.now()
    )


class DeviceConfig(Base):
    """Mirror of the controller's configuration (DECISIONS D-07: the controller is the authority)."""

    __tablename__ = "device_configs"

    device_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE"), primary_key=True
    )
    rev: Mapped[int] = mapped_column(Integer, nullable=False)
    doc: Mapped[dict[str, Any]] = mapped_column(JSONB, nullable=False)
    reported_ts: Mapped[int] = mapped_column(BigInteger, nullable=False)
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, server_default=func.now(), onupdate=func.now()
    )


class CommandStatus(enum.StrEnum):
    queued = "queued"
    sent = "sent"
    received = "received"
    applied = "applied"
    rejected = "rejected"
    expired = "expired"
    failed = "failed"


TERMINAL = {CommandStatus.applied, CommandStatus.rejected, CommandStatus.expired, CommandStatus.failed}


class Command(Base):
    """A remote command; `id` is the protocol command id (UUIDv7)."""

    __tablename__ = "commands"

    id: Mapped[uuid.UUID] = mapped_column(UUID(as_uuid=True), primary_key=True)
    device_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE"), nullable=False
    )
    home_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("homes.id", ondelete="CASCADE"), nullable=False
    )
    user_id: Mapped[uuid.UUID | None] = mapped_column(UUID(as_uuid=True), ForeignKey("users.id", ondelete="SET NULL"))
    op: Mapped[str] = mapped_column(String(24), nullable=False)
    target: Mapped[str | None] = mapped_column(String(48))
    args: Mapped[dict[str, Any] | None] = mapped_column(JSONB)
    status: Mapped[CommandStatus] = mapped_column(
        Enum(CommandStatus, name="command_status", values_callable=lambda e: [m.value for m in e]), nullable=False
    )
    code: Mapped[str | None] = mapped_column(String(32))
    message: Mapped[str | None] = mapped_column(String(160))
    idempotency_key: Mapped[str | None] = mapped_column(String(64))
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, server_default=func.now())
    expires_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
    sent_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    completed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))

    __table_args__ = (
        Index("ix_commands_device_created", "device_id", "created_at"),
        Index("ix_commands_open", "status", "expires_at", postgresql_where="status IN ('queued','sent','received')"),
        UniqueConstraint("user_id", "idempotency_key", name="uq_commands_user_idempotency"),
    )


class DeviceEvent(Base):
    """`events` messages from devices (inputs, scenes, automations). Kept 30 days."""

    __tablename__ = "device_events"

    id: Mapped[int] = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    device_id: Mapped[uuid.UUID] = mapped_column(
        UUID(as_uuid=True), ForeignKey("devices.id", ondelete="CASCADE"), nullable=False
    )
    received_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, server_default=func.now())
    payload: Mapped[dict[str, Any]] = mapped_column(JSONB, nullable=False)

    __table_args__ = (Index("ix_device_events_device_received", "device_id", "received_at"),)
