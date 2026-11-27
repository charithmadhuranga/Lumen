"""Devices, claims and local tickets (users), and gateway-facing registration.

Users (Bearer token, home roles):
  POST   /homes/{home}/gateway-claims     claim token for a gateway (setup code hash)
  POST   /homes/{home}/claims             claim token for a controller behind a gateway
  GET    /homes/{home}/devices
  GET    /devices/{id}, PATCH, DELETE
  GET    /gateways/{id}/children
  POST   /devices/{id}/local-ticket

Gateways (HTTP Basic: username g:{gatewayId}, password = its broker secret):
  POST   /gateway/register                (claim token + setup code instead of Basic)
  POST   /gateways/{id}/children/register
  GET    /gateway/children                its controllers incl. K_local (state recovery)
"""

from __future__ import annotations

import base64
import binascii
import uuid
from typing import Annotated, Any

from fastapi import APIRouter, Depends, Request, status
from sqlalchemy import select

from app.api.deps import CurrentPrincipal, DbSession, HomeAccess, check, home_access, load_home_access
from app.api.schemas import (
    ChildOut,
    ClaimOut,
    ControllerClaimCreate,
    ControllerRegister,
    ControllerRegistered,
    DeviceOut,
    DeviceUpdate,
    Envelope,
    GatewayClaimCreate,
    GatewayRegister,
    GatewayRegistered,
    LanHints,
    LocalTicketOut,
    LocalTicketRequest,
    MqttCredential,
)
from app.core.errors import ApiError, ok
from app.core.permissions import Action
from app.models.device import Device, DeviceKind
from app.models.home import Room
from app.services import devices as svc
from app.services.children import clear_children, publish_children
from app.services.keys import b64url
from app.services.ratelimit import RateLimiter

router = APIRouter(prefix="/api/v1", tags=["devices"])


def dump(model: Any) -> dict[str, Any]:
    return model.model_dump(mode="json", by_alias=True)  # type: ignore[no-any-return]


def device_out(d: Device) -> dict[str, Any]:
    return dump(
        DeviceOut(
            id=d.id,
            kind=d.kind.value,
            name=d.name,
            room_id=d.room_id,
            hw_model=d.hw_model,
            board=d.board,
            fw_version=d.fw_version,
            controller_id=d.controller_id,
            gateway_id=d.gateway_id,
            last_seen_at=d.last_seen_at,
            created_at=d.created_at,
        )
    )


async def _device_access(db: DbSession, principal: CurrentPrincipal, device_id: uuid.UUID) -> tuple[Device, HomeAccess]:
    device = await db.get(Device, device_id)
    if device is None:
        raise ApiError(404, "NOT_FOUND", "Device not found")
    try:
        access = await load_home_access(db, principal.user, device.home_id)
    except ApiError:
        raise ApiError(404, "NOT_FOUND", "Device not found") from None
    return device, access


# --- claims ---------------------------------------------------------------------------


@router.post("/homes/{home_id}/gateway-claims", status_code=status.HTTP_201_CREATED, response_model=Envelope[ClaimOut])
async def create_gateway_claim(
    body: GatewayClaimCreate,
    access: Annotated[HomeAccess, Depends(home_access(Action.devices_manage))],
    request: Request,
    db: DbSession,
) -> dict[str, Any]:
    token, expires = await svc.create_claim(
        db,
        request.app.state.settings,
        access.user,
        access.home.id,
        DeviceKind.gateway,
        proof_hash=bytes.fromhex(body.setup_proof_hash),
    )
    return ok(dump(ClaimOut(claim_token=token, expires_at=expires)))


@router.post("/homes/{home_id}/claims", status_code=status.HTTP_201_CREATED, response_model=Envelope[ClaimOut])
async def create_controller_claim(
    body: ControllerClaimCreate,
    access: Annotated[HomeAccess, Depends(home_access(Action.devices_manage))],
    request: Request,
    db: DbSession,
) -> dict[str, Any]:
    token, expires = await svc.create_claim(
        db, request.app.state.settings, access.user, access.home.id, DeviceKind.controller, gateway_id=body.gateway_id
    )
    return ok(dump(ClaimOut(claim_token=token, expires_at=expires)))


# --- devices --------------------------------------------------------------------------


@router.get("/homes/{home_id}/devices", response_model=Envelope[list[DeviceOut]])
async def list_devices(
    access: Annotated[HomeAccess, Depends(home_access(Action.home_read))], db: DbSession
) -> dict[str, Any]:
    rows = await db.scalars(select(Device).where(Device.home_id == access.home.id).order_by(Device.created_at))
    return ok([device_out(d) for d in rows])


@router.get("/devices/{device_id}", response_model=Envelope[DeviceOut])
async def get_device(device_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    device, access = await _device_access(db, principal, device_id)
    check(access, Action.home_read)
    return ok(device_out(device))


@router.patch("/devices/{device_id}", response_model=Envelope[DeviceOut])
async def update_device(
    device_id: uuid.UUID, body: DeviceUpdate, principal: CurrentPrincipal, db: DbSession
) -> dict[str, Any]:
    device, access = await _device_access(db, principal, device_id)
    check(access, Action.devices_manage)
    if body.name is not None:
        device.name = body.name
    if "room_id" in body.model_fields_set:
        if body.room_id is not None:
            room = await db.get(Room, body.room_id)
            if room is None or room.home_id != device.home_id:
                raise ApiError(404, "NOT_FOUND", "Room not found in this home")
        device.room_id = body.room_id
    await db.commit()
    await db.refresh(device)
    return ok(device_out(device))


@router.delete("/devices/{device_id}", response_model=Envelope[None])
async def delete_device(
    device_id: uuid.UUID, principal: CurrentPrincipal, request: Request, db: DbSession
) -> dict[str, Any]:
    device, access = await _device_access(db, principal, device_id)
    check(access, Action.devices_manage)
    kind, gateway_id = device.kind, device.gateway_id
    await svc.release(db, request.app.state.settings, device)
    publisher = request.app.state.publisher
    if kind == DeviceKind.gateway:
        await clear_children(publisher, device_id)
    elif gateway_id is not None:
        await publish_children(db, publisher, gateway_id)  # the gateway forgets this controller
    return ok(None)


@router.get("/gateways/{gateway_id}/children", response_model=Envelope[list[DeviceOut]])
async def list_children(gateway_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    gateway, access = await _device_access(db, principal, gateway_id)
    check(access, Action.home_read)
    if gateway.kind != DeviceKind.gateway:
        raise ApiError(404, "NOT_FOUND", "Gateway not found")
    return ok([device_out(d) for d in await svc.children_of(db, gateway.id)])


@router.post("/devices/{device_id}/local-ticket", response_model=Envelope[LocalTicketOut])
async def local_ticket(
    device_id: uuid.UUID, body: LocalTicketRequest, principal: CurrentPrincipal, request: Request, db: DbSession
) -> dict[str, Any]:
    device, access = await _device_access(db, principal, device_id)
    check(access, Action.control)
    ticket, expires = svc.mint_ticket(request.app.state.settings, device, principal.user, access.role, body.scope)
    return ok(
        dump(
            LocalTicketOut(
                ticket=ticket,
                scope=body.scope,
                expires_at=expires,
                device_id=device.id,
                lan=LanHints(controller_id=device.controller_id, url=device.lan_url),
            )
        )
    )


# --- gateway-facing ---------------------------------------------------------------------


def _ip(request: Request) -> str:
    return request.client.host if request.client else "unknown"


async def _limit_registrations(request: Request) -> None:
    limiter = RateLimiter(request.app.state.redis)
    key = f"device-register:{_ip(request)}"
    settings = request.app.state.settings
    if wait := await limiter.blocked(key, settings.register_device_ip_limit):
        raise ApiError(429, "RATE_LIMITED", "Too many registrations; try again later", {"Retry-After": str(wait)})
    await limiter.hit(key, 3600)


_GATEWAY_UNAUTHORIZED = ApiError(401, "UNAUTHORIZED", "Gateway authentication required", {"WWW-Authenticate": "Basic"})


async def current_gateway(request: Request, db: DbSession) -> Device:
    scheme, _, value = request.headers.get("authorization", "").partition(" ")
    if scheme.lower() != "basic" or not value or len(value) > 256:
        raise _GATEWAY_UNAUTHORIZED
    try:
        # The username is "g:{id}" and contains a colon; the secret (base64url) never does.
        username, _, secret = base64.b64decode(value, validate=True).decode().rpartition(":")
        gateway_id = uuid.UUID(username.removeprefix("g:")) if username.startswith("g:") else None
    except (binascii.Error, UnicodeDecodeError, ValueError):
        raise _GATEWAY_UNAUTHORIZED from None
    limiter = RateLimiter(request.app.state.redis)
    key = f"gateway-auth:{_ip(request)}"
    if wait := await limiter.blocked(key, 20):
        raise ApiError(429, "RATE_LIMITED", "Too many failed attempts", {"Retry-After": str(wait)})
    gateway = await svc.authenticate_gateway(db, gateway_id, secret) if gateway_id and secret else None
    if gateway is None:
        await limiter.hit(key, 300)
        raise _GATEWAY_UNAUTHORIZED
    return gateway


CurrentGateway = Annotated[Device, Depends(current_gateway)]


@router.post("/gateway/register", status_code=status.HTTP_201_CREATED, response_model=Envelope[GatewayRegistered])
async def register_gateway(body: GatewayRegister, request: Request, db: DbSession) -> dict[str, Any]:
    await _limit_registrations(request)
    settings = request.app.state.settings
    gateway, secret = await svc.register_gateway(
        db, settings, body.claim_token, body.setup_proof, body.hw.model_dump(), body.version
    )
    username = f"g:{gateway.id}"
    return ok(
        dump(
            GatewayRegistered(
                gateway_id=gateway.id, mqtt=MqttCredential(url=settings.broker_url, username=username, password=secret)
            )
        )
    )


@router.post(
    "/gateways/{gateway_id}/children/register",
    status_code=status.HTTP_201_CREATED,
    response_model=Envelope[ControllerRegistered],
)
async def register_child(
    gateway_id: uuid.UUID, body: ControllerRegister, gateway: CurrentGateway, request: Request, db: DbSession
) -> dict[str, Any]:
    if gateway_id != gateway.id:
        raise ApiError(403, "FORBIDDEN", "A gateway can only register its own children")
    await _limit_registrations(request)
    device, local_key = await svc.register_controller(db, request.app.state.settings, gateway, body.model_dump())
    await publish_children(db, request.app.state.publisher, gateway.id)
    return ok(
        dump(
            ControllerRegistered(
                device_id=device.id,
                local_key=b64url(local_key),
                ticket_epoch=device.ticket_epoch,
                gateway_id=gateway.id,
            )
        )
    )


@router.get("/gateway/children", response_model=Envelope[list[ChildOut]])
async def gateway_children(gateway: CurrentGateway, request: Request, db: DbSession) -> dict[str, Any]:
    settings = request.app.state.settings
    return ok(
        [
            dump(
                ChildOut(
                    device_id=d.id,
                    controller_id=d.controller_id,
                    lan_url=d.lan_url,
                    route_epoch=d.route_epoch,
                    ticket_epoch=d.ticket_epoch,
                    local_key=b64url(svc.local_key_of(settings, d)),
                )
            )
            for d in await svc.children_of(db, gateway.id)
        ]
    )
