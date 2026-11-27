"""Authentication routes (/api/v1/auth/*). Logic: app/services/accounts.py."""

from __future__ import annotations

import uuid
from typing import Annotated, Any

from fastapi import APIRouter, Depends, Request, status
from sqlalchemy import select

from app.api.deps import CurrentPrincipal, DbSession
from app.api.schemas import (
    ChangePasswordRequest,
    Envelope,
    LoginRequest,
    RefreshRequest,
    RegisterRequest,
    ResetConfirm,
    ResetRequest,
    SessionOut,
    TokenPair,
    TokenRequest,
    UserOut,
)
from app.core.errors import ApiError, ok
from app.models.account import Session, User
from app.services.accounts import Accounts, IssuedTokens, revoke_sessions
from app.services.ratelimit import RateLimiter

router = APIRouter(prefix="/api/v1/auth", tags=["auth"])


def accounts(request: Request, db: DbSession) -> Accounts:
    return Accounts(db, request.app.state.settings, RateLimiter(request.app.state.redis), request.app.state.email)


AccountsDep = Annotated[Accounts, Depends(accounts)]


def client_ip(request: Request) -> str:
    return request.client.host if request.client else "unknown"


def user_out(user: User) -> UserOut:
    return UserOut(
        id=user.id, email=user.email, email_verified=user.email_verified_at is not None, created_at=user.created_at
    )


def pair(tokens: IssuedTokens) -> dict[str, Any]:
    return ok(
        TokenPair(
            access_token=tokens.access_token,
            access_expires_at=tokens.access_expires_at,
            refresh_token=tokens.refresh_token,
            refresh_expires_at=tokens.refresh_expires_at,
            user=user_out(tokens.user),
        ).model_dump(mode="json", by_alias=True)
    )


@router.post("/register", status_code=status.HTTP_201_CREATED, response_model=Envelope[TokenPair])
async def register(body: RegisterRequest, request: Request, svc: AccountsDep) -> dict[str, Any]:
    return pair(await svc.register(body.email, body.password, client_ip(request), request.headers.get("user-agent")))


@router.post("/login", response_model=Envelope[TokenPair])
async def login(body: LoginRequest, request: Request, svc: AccountsDep) -> dict[str, Any]:
    return pair(await svc.login(body.email, body.password, client_ip(request), request.headers.get("user-agent")))


@router.post("/refresh", response_model=Envelope[TokenPair])
async def refresh(body: RefreshRequest, svc: AccountsDep) -> dict[str, Any]:
    return pair(await svc.refresh(body.refresh_token))


@router.post("/logout", response_model=Envelope[None])
async def logout(principal: CurrentPrincipal, svc: AccountsDep) -> dict[str, Any]:
    await svc.logout(principal.session_id)
    return ok(None)


@router.get("/me", response_model=Envelope[UserOut])
async def me(principal: CurrentPrincipal) -> dict[str, Any]:
    return ok(user_out(principal.user).model_dump(mode="json", by_alias=True))


@router.put("/password", response_model=Envelope[None])
async def change_password(body: ChangePasswordRequest, principal: CurrentPrincipal, svc: AccountsDep) -> dict[str, Any]:
    await svc.change_password(principal.user, principal.session_id, body.current_password, body.new_password)
    return ok(None)


@router.post("/verify-email", response_model=Envelope[None])
async def verify_email(body: TokenRequest, svc: AccountsDep) -> dict[str, Any]:
    await svc.verify_email(body.token)
    return ok(None)


@router.post("/verify-email/resend", status_code=status.HTTP_202_ACCEPTED, response_model=Envelope[None])
async def resend_verification(principal: CurrentPrincipal, svc: AccountsDep) -> dict[str, Any]:
    await svc.resend_verification(principal.user)
    return ok(None)


@router.post("/password-reset/request", status_code=status.HTTP_202_ACCEPTED, response_model=Envelope[None])
async def request_reset(body: ResetRequest, request: Request, svc: AccountsDep) -> dict[str, Any]:
    await svc.request_reset(body.email, client_ip(request))
    return ok(None)


@router.post("/password-reset/confirm", response_model=Envelope[None])
async def confirm_reset(body: ResetConfirm, svc: AccountsDep) -> dict[str, Any]:
    await svc.confirm_reset(body.token, body.new_password)
    return ok(None)


@router.get("/sessions", response_model=Envelope[list[SessionOut]])
async def list_sessions(principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    rows = (
        await db.execute(
            select(Session)
            .where(Session.user_id == principal.user.id, Session.revoked_at.is_(None))
            .order_by(Session.last_used_at.desc())
            .limit(50)
        )
    ).scalars()
    return ok(
        [
            SessionOut(
                id=s.id,
                created_at=s.created_at,
                last_used_at=s.last_used_at,
                user_agent=s.user_agent,
                current=s.id == principal.session_id,
            ).model_dump(mode="json", by_alias=True)
            for s in rows
        ]
    )


@router.delete("/sessions/{session_id}", response_model=Envelope[None])
async def revoke_session(session_id: uuid.UUID, principal: CurrentPrincipal, db: DbSession) -> dict[str, Any]:
    session = await db.get(Session, session_id)
    if session is None or session.user_id != principal.user.id or session.revoked_at is not None:
        raise ApiError(404, "NOT_FOUND", "Session not found")
    await revoke_sessions(db, Session.id == session_id, "revoked_by_user")
    await db.commit()
    return ok(None)
