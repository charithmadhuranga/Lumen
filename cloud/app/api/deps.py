"""Request dependencies: the signed-in user and home access.

Tenant isolation lives here: `home_access` returns 404 (not 403) when the user
is not a member, so another home's IDs cannot even be confirmed to exist.
"""

from __future__ import annotations

import uuid
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Annotated

from fastapi import Depends, Request
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.core.permissions import Action, allowed
from app.core.security import TokenError, decode_access_token
from app.db.session import get_session
from app.models.account import Session, User, UserStatus
from app.models.home import Home, HomeMember, Role

DbSession = Annotated[AsyncSession, Depends(get_session)]

_UNAUTHORIZED = ApiError(401, "UNAUTHORIZED", "Sign in required", {"WWW-Authenticate": "Bearer"})


@dataclass(frozen=True)
class Principal:
    user: User
    session_id: uuid.UUID


def bearer_token(request: Request) -> str | None:
    header = request.headers.get("authorization", "")
    scheme, _, value = header.partition(" ")
    if scheme.lower() != "bearer" or not value or len(value) > 2048:
        return None
    return value.strip()


async def current_principal(request: Request, db: DbSession) -> Principal:
    token = bearer_token(request)
    if token is None:
        raise _UNAUTHORIZED
    try:
        claims = decode_access_token(request.app.state.settings, token)
    except TokenError:
        raise _UNAUTHORIZED from None
    row = (
        await db.execute(
            select(User, Session)
            .join(Session, Session.user_id == User.id)
            .where(User.id == claims.user_id, Session.id == claims.session_id)
        )
    ).first()
    # Signed out (session revoked) or disabled accounts lose access at once,
    # not when the 15-minute token expires.
    if row is None or row.Session.revoked_at is not None or row.User.status != UserStatus.active:
        raise _UNAUTHORIZED
    return Principal(row.User, claims.session_id)


CurrentPrincipal = Annotated[Principal, Depends(current_principal)]


@dataclass(frozen=True)
class HomeAccess:
    home: Home
    role: Role
    user: User


def _not_found() -> ApiError:
    return ApiError(404, "NOT_FOUND", "Home not found")


async def load_home_access(db: AsyncSession, user: User, home_id: uuid.UUID) -> HomeAccess:
    row = (
        await db.execute(
            select(Home, HomeMember.role)
            .join(HomeMember, HomeMember.home_id == Home.id)
            .where(Home.id == home_id, HomeMember.user_id == user.id)
        )
    ).first()
    if row is None:
        raise _not_found()
    return HomeAccess(row.Home, row.role, user)


def check(access: HomeAccess, action: Action) -> None:
    if not allowed(access.role, action):
        raise ApiError(403, "FORBIDDEN", f"Your role ({access.role.value}) cannot do this")


def home_access(action: Action) -> Callable[..., Awaitable[HomeAccess]]:
    """Dependency for routes with a {home_id} path parameter."""

    async def dependency(home_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> HomeAccess:
        access = await load_home_access(db, principal.user, home_id)
        check(access, action)
        return access

    return dependency


def now() -> datetime:
    return datetime.now(UTC)
