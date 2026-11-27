"""Request/response models. JSON is camelCase, matching the controller API and
the app's Zod schemas; Python stays snake_case."""

from __future__ import annotations

import re
import uuid
import zoneinfo
from datetime import datetime
from typing import Annotated

from pydantic import AfterValidator, BaseModel, ConfigDict, Field, StringConstraints
from pydantic.alias_generators import to_camel

from app.models.home import Role

_EMAIL = re.compile(r"^[^@\s]{1,64}@[^@\s.]+(\.[^@\s.]+)+$")


class ApiModel(BaseModel):
    model_config = ConfigDict(alias_generator=to_camel, populate_by_name=True, extra="forbid", frozen=True)


class Envelope[T](BaseModel):
    success: bool = True
    data: T
    error: None = None


def _email(value: str) -> str:
    value = value.strip().lower()
    if len(value) > 254 or not _EMAIL.match(value):
        raise ValueError("not a valid e-mail address")
    return value


def _timezone(value: str) -> str:
    if value not in zoneinfo.available_timezones():
        raise ValueError("unknown IANA time zone")
    return value


Email = Annotated[str, StringConstraints(max_length=320), AfterValidator(_email)]
# Length checks only here; the content rules (not the e-mail) are in services.accounts.
Password = Annotated[str, StringConstraints(min_length=10, max_length=128)]
Name = Annotated[str, StringConstraints(strip_whitespace=True, min_length=1, max_length=64)]
TimeZone = Annotated[str, StringConstraints(max_length=64), AfterValidator(_timezone)]
OpaqueToken = Annotated[str, StringConstraints(min_length=20, max_length=100, pattern=r"^[a-z]{3}_[A-Za-z0-9_-]{43}$")]


# --- auth -----------------------------------------------------------------------


class RegisterRequest(ApiModel):
    email: Email
    password: Password


class LoginRequest(ApiModel):
    email: Email
    password: Annotated[str, StringConstraints(min_length=1, max_length=128)]


class RefreshRequest(ApiModel):
    refresh_token: OpaqueToken


class ChangePasswordRequest(ApiModel):
    current_password: Annotated[str, StringConstraints(min_length=1, max_length=128)]
    new_password: Password


class TokenRequest(ApiModel):
    token: OpaqueToken


class ResetRequest(ApiModel):
    email: Email


class ResetConfirm(ApiModel):
    token: OpaqueToken
    new_password: Password


class UserOut(ApiModel):
    id: uuid.UUID
    email: str
    email_verified: bool
    created_at: datetime


class TokenPair(ApiModel):
    access_token: str
    access_expires_at: datetime
    refresh_token: str
    refresh_expires_at: datetime
    user: UserOut


class SessionOut(ApiModel):
    id: uuid.UUID
    created_at: datetime
    last_used_at: datetime
    user_agent: str | None
    current: bool


# --- homes ----------------------------------------------------------------------


class HomeCreate(ApiModel):
    name: Name
    timezone: TimeZone


class HomeUpdate(ApiModel):
    name: Name | None = None
    timezone: TimeZone | None = None


class HomeOut(ApiModel):
    id: uuid.UUID
    name: str
    timezone: str
    role: Role
    created_at: datetime


class MemberOut(ApiModel):
    user_id: uuid.UUID
    email: str
    role: Role
    joined_at: datetime


class MemberRoleUpdate(ApiModel):
    role: Role


class TransferRequest(ApiModel):
    user_id: uuid.UUID


class RoomCreate(ApiModel):
    name: Name
    sort_order: Annotated[int, Field(ge=0, le=10000)] = 0


class RoomUpdate(ApiModel):
    name: Name | None = None
    sort_order: Annotated[int, Field(ge=0, le=10000)] | None = None


class RoomOut(ApiModel):
    id: uuid.UUID
    home_id: uuid.UUID
    name: str
    sort_order: int
