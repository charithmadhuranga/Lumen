"""Account logic: registration, sign-in, refresh rotation, sign-out, password
change and reset, e-mail verification. Routes in app/api/v1/auth.py stay thin.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta

import structlog
from sqlalchemy import ColumnElement, select, update
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.errors import ApiError
from app.core.security import (
    hash_password,
    issue_access_token,
    new_opaque_token,
    password_needs_rehash,
    token_digest,
    verify_password,
)
from app.core.settings import Settings
from app.models.account import EmailToken, EmailTokenPurpose, RefreshToken, Session, User, UserStatus
from app.services.email import EmailMessage, EmailSender
from app.services.ratelimit import RateLimiter

log = structlog.get_logger("lumen.accounts")

VERIFY_TTL = timedelta(days=2)
RESET_TTL = timedelta(minutes=30)


def _now() -> datetime:
    return datetime.now(UTC)


def check_password_rules(password: str, email: str) -> None:
    lowered = password.lower()
    local = email.split("@", 1)[0]
    based_on_email = len(local) >= 4 and local in lowered and len(password) < 16
    if lowered in (email, local) or based_on_email:
        raise ApiError(400, "WEAK_PASSWORD", "The password must not be (or be based on) your e-mail address")
    if len(set(password)) < 4:
        raise ApiError(400, "WEAK_PASSWORD", "The password uses too few different characters")


@dataclass(frozen=True)
class IssuedTokens:
    access_token: str
    access_expires_at: datetime
    refresh_token: str
    refresh_expires_at: datetime
    user: User


class Accounts:
    def __init__(self, db: AsyncSession, settings: Settings, limiter: RateLimiter, email: EmailSender) -> None:
        self.db = db
        self.settings = settings
        self.limiter = limiter
        self.email = email

    # --- tokens ---------------------------------------------------------------

    async def _issue(self, user: User, session: Session, parent: uuid.UUID | None = None) -> IssuedTokens:
        refresh, digest = new_opaque_token("lrt_")
        expires = _now() + timedelta(days=self.settings.refresh_ttl_days)
        self.db.add(RefreshToken(session_id=session.id, token_hash=digest, parent_id=parent, expires_at=expires))
        access, access_exp = issue_access_token(self.settings, user.id, session.id)
        return IssuedTokens(access, access_exp, refresh, expires, user)

    async def _new_session(self, user: User, user_agent: str | None) -> IssuedTokens:
        session = Session(user_id=user.id, user_agent=(user_agent or "")[:200] or None)
        self.db.add(session)
        await self.db.flush()
        return await self._issue(user, session)

    # --- registration and sign-in --------------------------------------------

    async def register(self, email: str, password: str, ip: str, user_agent: str | None) -> IssuedTokens:
        key = f"register:{ip}"
        if wait := await self.limiter.blocked(key, self.settings.register_ip_limit):
            raise _rate_limited(wait)
        await self.limiter.hit(key, self.settings.register_window_s)
        check_password_rules(password, email)
        user = User(email=email, password_hash=hash_password(password))
        self.db.add(user)
        try:
            await self.db.flush()
        except IntegrityError:
            await self.db.rollback()
            # Registration is rate-limited per IP, which bounds how fast this can
            # be used to test whether an address has an account.
            raise ApiError(409, "EMAIL_IN_USE", "An account with this e-mail already exists") from None
        tokens = await self._new_session(user, user_agent)
        await self._send_token(user, EmailTokenPurpose.verify_email, VERIFY_TTL)
        await self.db.commit()
        log.info("user_registered", user_id=str(user.id))
        return tokens

    async def login(self, email: str, password: str, ip: str, user_agent: str | None) -> IssuedTokens:
        pair_key, ip_key = f"login:{ip}:{email}", f"login-ip:{ip}"
        for key, limit in ((pair_key, self.settings.login_fail_limit), (ip_key, self.settings.login_ip_fail_limit)):
            if wait := await self.limiter.blocked(key, limit):
                raise _rate_limited(wait)
        user = (await self.db.execute(select(User).where(User.email == email))).scalar_one_or_none()
        # Always run one argon2 verification, so timing does not reveal whether the account exists.
        ok = verify_password(password, user.password_hash if user else None)
        locked = user is not None and user.locked_until is not None and user.locked_until > _now()
        if not ok or user is None or locked or user.status != UserStatus.active:
            await self.limiter.hit(pair_key, self.settings.login_window_s)
            await self.limiter.hit(ip_key, self.settings.login_window_s)
            if user is not None and not locked and not ok:
                user.failed_logins += 1
                if user.failed_logins >= self.settings.lockout_after:
                    user.locked_until = _now() + timedelta(seconds=self.settings.lockout_s)
                    user.failed_logins = 0
                    log.warning("account_locked", user_id=str(user.id))
                await self.db.commit()
            if locked:
                raise ApiError(423, "ACCOUNT_LOCKED", "Too many failed sign-ins; try again in a few minutes")
            raise ApiError(401, "INVALID_CREDENTIALS", "Wrong e-mail or password")
        user.failed_logins = 0
        user.locked_until = None
        if password_needs_rehash(user.password_hash):
            user.password_hash = hash_password(password)
        await self.limiter.clear(pair_key)
        tokens = await self._new_session(user, user_agent)
        await self.db.commit()
        return tokens

    async def refresh(self, refresh_token: str) -> IssuedTokens:
        digest = token_digest(refresh_token)
        row = (
            await self.db.execute(
                select(RefreshToken, Session, User)
                .join(Session, Session.id == RefreshToken.session_id)
                .join(User, User.id == Session.user_id)
                .where(RefreshToken.token_hash == digest)
                .with_for_update(of=RefreshToken)
            )
        ).first()
        invalid = ApiError(401, "INVALID_REFRESH_TOKEN", "Sign in again")
        if row is None:
            raise invalid
        token, session, user = row.RefreshToken, row.Session, row.User
        if session.revoked_at is not None or user.status != UserStatus.active:
            raise invalid
        if token.used_at is not None:
            # A rotated token came back: someone else holds a copy. End the session.
            session.revoked_at, session.revoke_reason = _now(), "refresh_reuse"
            await self.db.commit()
            log.warning("refresh_token_reuse", user_id=str(user.id), session_id=str(session.id))
            raise invalid
        if token.expires_at <= _now():
            raise invalid
        token.used_at = _now()
        session.last_used_at = _now()
        tokens = await self._issue(user, session, parent=token.id)
        await self.db.commit()
        return tokens

    async def logout(self, session_id: uuid.UUID) -> None:
        await self._revoke(Session.id == session_id, "logout")
        await self.db.commit()

    async def _revoke(self, condition: ColumnElement[bool], reason: str) -> None:
        await revoke_sessions(self.db, condition, reason)

    # --- password -------------------------------------------------------------

    async def change_password(self, user: User, session_id: uuid.UUID, current: str, new: str) -> None:
        if not verify_password(current, user.password_hash):
            raise ApiError(400, "CURRENT_PASSWORD_WRONG", "The current password is wrong")
        check_password_rules(new, user.email)
        user.password_hash = hash_password(new)
        # Every other installation must sign in again; this one stays signed in.
        await self._revoke((Session.user_id == user.id) & (Session.id != session_id), "password_changed")
        await self.db.commit()

    async def request_reset(self, email: str, ip: str) -> None:
        """Always succeeds from the caller's view (no account enumeration)."""
        key = f"reset:{ip}:{email}"
        if await self.limiter.blocked(key, 3):
            return
        await self.limiter.hit(key, 3600)
        user = (await self.db.execute(select(User).where(User.email == email))).scalar_one_or_none()
        if user is None or user.status != UserStatus.active:
            return
        await self._send_token(user, EmailTokenPurpose.reset_password, RESET_TTL)
        await self.db.commit()

    async def confirm_reset(self, token: str, new_password: str) -> None:
        user, record = await self._consume(token, EmailTokenPurpose.reset_password)
        check_password_rules(new_password, user.email)
        user.password_hash = hash_password(new_password)
        user.failed_logins, user.locked_until = 0, None
        await self._revoke(Session.user_id == user.id, "password_reset")
        # A reset also proves control of the mailbox.
        user.email_verified_at = user.email_verified_at or _now()
        record.used_at = _now()
        await self.db.commit()

    # --- e-mail verification -------------------------------------------------

    async def verify_email(self, token: str) -> None:
        user, record = await self._consume(token, EmailTokenPurpose.verify_email)
        user.email_verified_at = user.email_verified_at or _now()
        record.used_at = _now()
        await self.db.commit()

    async def resend_verification(self, user: User) -> None:
        if user.email_verified_at is not None:
            return
        key = f"verify:{user.id}"
        if wait := await self.limiter.blocked(key, 3):
            raise _rate_limited(wait)
        await self.limiter.hit(key, 3600)
        await self._send_token(user, EmailTokenPurpose.verify_email, VERIFY_TTL)
        await self.db.commit()

    async def _send_token(self, user: User, purpose: EmailTokenPurpose, ttl: timedelta) -> None:
        # One live token per purpose: issuing a new one invalidates the older ones.
        await self.db.execute(
            update(EmailToken)
            .where(EmailToken.user_id == user.id, EmailToken.purpose == purpose, EmailToken.used_at.is_(None))
            .values(used_at=_now())
        )
        prefix = "lev_" if purpose == EmailTokenPurpose.verify_email else "lpr_"
        token, digest = new_opaque_token(prefix)
        self.db.add(EmailToken(user_id=user.id, purpose=purpose, token_hash=digest, expires_at=_now() + ttl))
        await self.db.flush()
        await self.email.send(EmailMessage(to=user.email, kind=purpose.value, token=token))

    async def _consume(self, token: str, purpose: EmailTokenPurpose) -> tuple[User, EmailToken]:
        row = (
            await self.db.execute(
                select(EmailToken, User)
                .join(User, User.id == EmailToken.user_id)
                .where(EmailToken.token_hash == token_digest(token), EmailToken.purpose == purpose)
                .with_for_update(of=EmailToken)
            )
        ).first()
        if row is None or row.EmailToken.used_at is not None or row.EmailToken.expires_at <= _now():
            raise ApiError(400, "INVALID_TOKEN", "This link is invalid or has expired")
        return row.User, row.EmailToken


async def revoke_sessions(db: AsyncSession, condition: ColumnElement[bool], reason: str) -> None:
    """Revoke every live session matching `condition` (does not commit)."""
    await db.execute(
        update(Session).where(condition, Session.revoked_at.is_(None)).values(revoked_at=_now(), revoke_reason=reason)
    )


def _rate_limited(wait: int) -> ApiError:
    return ApiError(429, "RATE_LIMITED", "Too many attempts; try again later", {"Retry-After": str(wait)})
