"""Publishes a gateway's retained assignment list `lumen/v1/g/{gw}/children` (PROTOCOL §2).

Called after a controller is registered or removed and when a gateway is
removed (an empty retained payload clears the topic). Best effort: if the
broker is unreachable the gateway still gets the truth from
GET /api/v1/gateway/children, and the broker ACL follows the database.
"""

from __future__ import annotations

import time
import uuid

import structlog
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app import protocol
from app.models.device import Device, DeviceKind
from app.mqtt.bridge import BridgeUnavailable, Publisher

log = structlog.get_logger("lumen.children")


async def publish_children(db: AsyncSession, publisher: Publisher, gateway_id: uuid.UUID) -> None:
    rows = (
        await db.scalars(
            select(Device)
            .where(Device.gateway_id == gateway_id, Device.kind == DeviceKind.controller)
            .order_by(Device.created_at)
        )
    ).all()
    message = protocol.Children(
        v=1,
        gateway=gateway_id,
        ts=int(time.time() * 1000),
        children=[protocol.Child(device=d.id, route_epoch=d.route_epoch, controller_id=d.controller_id) for d in rows],
    )
    try:
        await publisher.publish(f"lumen/v1/g/{gateway_id}/children", message.wire(), retain=True)
    except BridgeUnavailable:
        log.warning("children_publish_failed", gateway_id=str(gateway_id))


async def clear_children(publisher: Publisher, gateway_id: uuid.UUID) -> None:
    try:
        await publisher.publish(f"lumen/v1/g/{gateway_id}/children", b"", retain=True)
    except BridgeUnavailable:
        log.warning("children_clear_failed", gateway_id=str(gateway_id))
