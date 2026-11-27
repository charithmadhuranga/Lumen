"""Homes, members and cloud rooms.

Every route resolves the home through the caller's membership (app/api/deps.py),
so a home the caller does not belong to is 404, never 403. A home has exactly
one owner; ownership moves only through /transfer.
"""

from __future__ import annotations

import uuid
from typing import Annotated, Any

from fastapi import APIRouter, Depends, status
from sqlalchemy import func, select
from sqlalchemy.exc import IntegrityError

from app.api.deps import CurrentPrincipal, DbSession, HomeAccess, check, home_access, load_home_access
from app.api.schemas import (
    Envelope,
    HomeCreate,
    HomeOut,
    HomeUpdate,
    MemberOut,
    MemberRoleUpdate,
    RoomCreate,
    RoomOut,
    RoomUpdate,
    TransferRequest,
)
from app.core.errors import ApiError, ok
from app.core.permissions import RANK, Action
from app.models.account import User
from app.models.home import Home, HomeMember, Role, Room

router = APIRouter(prefix="/api/v1", tags=["homes"])

MAX_HOMES_PER_USER = 20
MAX_ROOMS_PER_HOME = 100


def home_out(home: Home, role: Role) -> dict[str, Any]:
    return HomeOut(
        id=home.id, name=home.name, timezone=home.timezone, role=role, created_at=home.created_at
    ).model_dump(mode="json", by_alias=True)


def room_out(room: Room) -> dict[str, Any]:
    return RoomOut(id=room.id, home_id=room.home_id, name=room.name, sort_order=room.sort_order).model_dump(
        mode="json", by_alias=True
    )


# --- homes ----------------------------------------------------------------------


@router.get("/homes", response_model=Envelope[list[HomeOut]])
async def list_homes(principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    rows = await db.execute(
        select(Home, HomeMember.role)
        .join(HomeMember, HomeMember.home_id == Home.id)
        .where(HomeMember.user_id == principal.user.id)
        .order_by(Home.created_at)
    )
    return ok([home_out(r.Home, r.role) for r in rows])


@router.post("/homes", status_code=status.HTTP_201_CREATED, response_model=Envelope[HomeOut])
async def create_home(body: HomeCreate, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    count = await db.scalar(select(func.count()).where(HomeMember.user_id == principal.user.id))
    if (count or 0) >= MAX_HOMES_PER_USER:
        raise ApiError(409, "CAPACITY", f"An account can belong to at most {MAX_HOMES_PER_USER} homes")
    home = Home(name=body.name, timezone=body.timezone, owner_id=principal.user.id)
    db.add(home)
    await db.flush()
    db.add(HomeMember(home_id=home.id, user_id=principal.user.id, role=Role.owner))
    await db.commit()
    await db.refresh(home)
    return ok(home_out(home, Role.owner))


@router.get("/homes/{home_id}", response_model=Envelope[HomeOut])
async def get_home(access: Annotated[HomeAccess, Depends(home_access(Action.home_read))]) -> dict[str, Any]:
    return ok(home_out(access.home, access.role))


@router.patch("/homes/{home_id}", response_model=Envelope[HomeOut])
async def update_home(
    body: HomeUpdate, access: Annotated[HomeAccess, Depends(home_access(Action.home_update))], db: DbSession
) -> dict[str, Any]:
    if body.name is not None:
        access.home.name = body.name
    if body.timezone is not None:
        access.home.timezone = body.timezone
    await db.commit()
    await db.refresh(access.home)
    return ok(home_out(access.home, access.role))


@router.delete("/homes/{home_id}", response_model=Envelope[None])
async def delete_home(
    access: Annotated[HomeAccess, Depends(home_access(Action.home_delete))], db: DbSession
) -> dict[str, Any]:
    # P3 extends this: devices are released (credentials revoked, epochs raised) first.
    await db.delete(access.home)
    await db.commit()
    return ok(None)


# --- members --------------------------------------------------------------------


@router.get("/homes/{home_id}/members", response_model=Envelope[list[MemberOut]])
async def list_members(
    access: Annotated[HomeAccess, Depends(home_access(Action.members_read))], db: DbSession
) -> dict[str, Any]:
    rows = await db.execute(
        select(HomeMember, User.email)
        .join(User, User.id == HomeMember.user_id)
        .where(HomeMember.home_id == access.home.id)
        .order_by(HomeMember.created_at)
    )
    return ok(
        [
            MemberOut(
                user_id=r.HomeMember.user_id, email=r.email, role=r.HomeMember.role, joined_at=r.HomeMember.created_at
            ).model_dump(mode="json", by_alias=True)
            for r in rows
        ]
    )


async def _member(db: DbSession, home_id: uuid.UUID, user_id: uuid.UUID) -> HomeMember:
    member = (
        await db.execute(select(HomeMember).where(HomeMember.home_id == home_id, HomeMember.user_id == user_id))
    ).scalar_one_or_none()
    if member is None:
        raise ApiError(404, "NOT_FOUND", "Member not found")
    return member


@router.patch("/homes/{home_id}/members/{user_id}", response_model=Envelope[None])
async def change_role(
    user_id: uuid.UUID,
    body: MemberRoleUpdate,
    access: Annotated[HomeAccess, Depends(home_access(Action.members_manage))],
    db: DbSession,
) -> dict[str, Any]:
    if user_id == access.user.id:
        raise ApiError(400, "INVALID_REQUEST", "You cannot change your own role")
    if body.role == Role.owner:
        raise ApiError(400, "INVALID_REQUEST", "Use transfer to make someone the owner")
    member = await _member(db, access.home.id, user_id)
    if RANK[member.role] >= RANK[access.role] or RANK[body.role] >= RANK[access.role]:
        raise ApiError(403, "FORBIDDEN", "You can only manage members with a lower role than yours")
    member.role = body.role
    await db.commit()
    return ok(None)


@router.delete("/homes/{home_id}/members/{user_id}", response_model=Envelope[None])
async def remove_member(
    user_id: uuid.UUID, home_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession
) -> dict[str, Any]:
    access = await load_home_access(db, principal.user, home_id)
    member = await _member(db, home_id, user_id)
    if user_id == principal.user.id:
        if member.role == Role.owner:
            raise ApiError(409, "OWNER_MUST_TRANSFER", "Transfer the home to another member before leaving")
    else:
        check(access, Action.members_manage)
        if RANK[member.role] >= RANK[access.role]:
            raise ApiError(403, "FORBIDDEN", "You can only remove members with a lower role than yours")
    await db.delete(member)
    await db.commit()
    return ok(None)


@router.post("/homes/{home_id}/transfer", response_model=Envelope[None])
async def transfer_ownership(
    body: TransferRequest,
    access: Annotated[HomeAccess, Depends(home_access(Action.home_transfer))],
    db: DbSession,
) -> dict[str, Any]:
    if body.user_id == access.user.id:
        raise ApiError(400, "INVALID_REQUEST", "You already own this home")
    target = await _member(db, access.home.id, body.user_id)
    me = await _member(db, access.home.id, access.user.id)
    me.role = Role.admin
    target.role = Role.owner
    access.home.owner_id = target.user_id
    await db.commit()
    return ok(None)


# --- rooms ----------------------------------------------------------------------


@router.get("/homes/{home_id}/rooms", response_model=Envelope[list[RoomOut]])
async def list_rooms(
    access: Annotated[HomeAccess, Depends(home_access(Action.rooms_read))], db: DbSession
) -> dict[str, Any]:
    rows = await db.scalars(select(Room).where(Room.home_id == access.home.id).order_by(Room.sort_order, Room.name))
    return ok([room_out(r) for r in rows])


@router.post("/homes/{home_id}/rooms", status_code=status.HTTP_201_CREATED, response_model=Envelope[RoomOut])
async def create_room(
    body: RoomCreate, access: Annotated[HomeAccess, Depends(home_access(Action.rooms_write))], db: DbSession
) -> dict[str, Any]:
    count = await db.scalar(select(func.count()).where(Room.home_id == access.home.id))
    if (count or 0) >= MAX_ROOMS_PER_HOME:
        raise ApiError(409, "CAPACITY", f"A home can have at most {MAX_ROOMS_PER_HOME} rooms")
    room = Room(home_id=access.home.id, name=body.name, sort_order=body.sort_order)
    db.add(room)
    await _commit_room(db)
    await db.refresh(room)
    return ok(room_out(room))


async def _room_access(db: DbSession, principal: CurrentPrincipal, room_id: uuid.UUID, action: Action) -> Room:
    room = await db.get(Room, room_id)
    if room is None:
        raise ApiError(404, "NOT_FOUND", "Room not found")
    try:
        access = await load_home_access(db, principal.user, room.home_id)
    except ApiError:
        raise ApiError(404, "NOT_FOUND", "Room not found") from None
    check(access, action)
    return room


async def _commit_room(db: DbSession) -> None:
    try:
        await db.commit()
    except IntegrityError:
        await db.rollback()
        raise ApiError(409, "CONFLICT", "A room with this name already exists in the home") from None


@router.get("/rooms/{room_id}", response_model=Envelope[RoomOut])
async def get_room(room_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    return ok(room_out(await _room_access(db, principal, room_id, Action.rooms_read)))


@router.patch("/rooms/{room_id}", response_model=Envelope[RoomOut])
async def update_room(
    room_id: uuid.UUID, body: RoomUpdate, principal: CurrentPrincipal, db: DbSession
) -> dict[str, Any]:
    room = await _room_access(db, principal, room_id, Action.rooms_write)
    if body.name is not None:
        room.name = body.name
    if body.sort_order is not None:
        room.sort_order = body.sort_order
    await _commit_room(db)
    await db.refresh(room)
    return ok(room_out(room))


@router.delete("/rooms/{room_id}", response_model=Envelope[None])
async def delete_room(room_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    room = await _room_access(db, principal, room_id, Action.rooms_write)
    await db.delete(room)
    await db.commit()
    return ok(None)
