"""Pydantic models of the wire protocol (docs/pro/PROTOCOL.md, protocol/schemas).

Contract-tested against protocol/fixtures (tests/test_protocol_models.py): every
valid fixture must parse, every invalid one must fail. Keys are camelCase on
the wire.
"""

from __future__ import annotations

import re
import uuid
from typing import Annotated, Literal, Self

from pydantic import BaseModel, ConfigDict, Field, StringConstraints, model_validator
from pydantic.alias_generators import to_camel

LocalId = Annotated[str, StringConstraints(pattern=r"^[a-z0-9_-]{1,48}$")]
_LOCAL_ID = re.compile(r"[a-z0-9_-]{1,48}")
TimestampMs = Annotated[int, Field(ge=1_600_000_000_000, le=4_102_444_800_000, strict=True)]
Percent = Annotated[int, Field(ge=0, le=100, strict=True)]
Byte = Annotated[int, Field(ge=0, le=255, strict=True)]
Kelvin = Annotated[int, Field(ge=1000, le=20000, strict=True)]
LightType = Literal["switch", "dimmer", "rgb", "rgbw", "cct"]
SemVer = Annotated[str, StringConstraints(max_length=32, pattern=r"^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$")]
Name = Annotated[str, StringConstraints(min_length=1, max_length=64)]
Version = Literal[1]


class Wire(BaseModel):
    model_config = ConfigDict(alias_generator=to_camel, populate_by_name=True, extra="forbid", frozen=True)

    def wire(self) -> dict[str, object]:
        return self.model_dump(mode="json", by_alias=True, exclude_none=True)


# --- commands (§3) ------------------------------------------------------------------------


class LightArgs(Wire):
    power: bool | None = None
    toggle: Literal[True] | None = None
    brightness: Percent | None = None
    red: Byte | None = None
    green: Byte | None = None
    blue: Byte | None = None
    white: Byte | None = None
    color_temperature: Kelvin | None = None
    transition_ms: Annotated[int, Field(ge=0, le=600_000, strict=True)] | None = None

    @model_validator(mode="after")
    def _rules(self) -> Self:
        if not self.model_fields_set:
            raise ValueError("at least one argument is required")
        if self.power is not None and self.toggle is not None:
            raise ValueError("power and toggle are exclusive")
        return self


class EffectArgs(Wire):
    effect: Annotated[str, StringConstraints(pattern=r"^[a-z0-9_-]{1,24}$")]
    speed_ms: Annotated[int, Field(ge=20, le=60_000, strict=True)] | None = None
    duration_ms: Annotated[int, Field(ge=0, le=86_400_000, strict=True)] | None = None


class RoomArgs(Wire):
    power: bool | None = None
    brightness: Percent | None = None

    @model_validator(mode="after")
    def _rules(self) -> Self:
        if not self.model_fields_set:
            raise ValueError("at least one argument is required")
        return self


class ConfigArgs(Wire):
    base_revision: Annotated[int, Field(ge=0, strict=True)]
    action: Literal["upsert", "delete"]
    id: LocalId | None = None
    doc: dict[str, object] | None = None

    @model_validator(mode="after")
    def _rules(self) -> Self:
        if self.action == "upsert" and self.doc is None:
            raise ValueError("upsert needs doc")
        if self.action == "delete" and (self.id is None or self.doc is not None):
            raise ValueError("delete needs id and no doc")
        return self


class Source(Wire):
    kind: Literal["user", "automation", "system"]
    id: Annotated[str, StringConstraints(min_length=1, max_length=40)]


Op = Literal[
    "light.set",
    "light.effect",
    "group.set",
    "room.set",
    "scene.activate",
    "device.status",
    "device.restart",
    "config.apply",
]
_ARGS: dict[str, type[Wire] | None] = {
    "light.set": LightArgs,
    "group.set": LightArgs,
    "light.effect": EffectArgs,
    "room.set": RoomArgs,
    "scene.activate": None,
    "device.status": None,
    "device.restart": None,
    "config.apply": ConfigArgs,
}
TARGETED = {"light.set", "group.set", "light.effect", "room.set", "scene.activate", "config.apply"}
CONFIG_KINDS = {"light", "room", "group", "scene", "automation", "cloud"}


class Command(Wire):
    v: Version
    id: uuid.UUID
    ts: TimestampMs
    exp: TimestampMs
    src: Source
    op: Op
    target: Annotated[str, StringConstraints(max_length=48)] | None = None
    args: dict[str, object] | None = None

    @model_validator(mode="after")
    def _shape(self) -> Self:
        if self.op in TARGETED:
            if self.target is None:
                raise ValueError(f"{self.op} needs a target")
            if self.op == "config.apply":
                if self.target not in CONFIG_KINDS:
                    raise ValueError("unknown config kind")
            elif not _LOCAL_ID.fullmatch(self.target):
                raise ValueError("target is not a local id")
        elif self.target is not None or self.args is not None:
            raise ValueError(f"{self.op} takes no target or args")
        model = _ARGS[self.op]
        if model is None:
            if self.args:
                raise ValueError(f"{self.op} takes no args")
        elif self.args is None:
            raise ValueError(f"{self.op} needs args")
        else:
            model.model_validate(self.args)
        return self


# --- results (§4) --------------------------------------------------------------------------

REJECT_CODES = {
    "invalid",
    "unknown_target",
    "unsupported_property",
    "unsupported",
    "forbidden",
    "conflict",
    "clock_unsynced",
    "capacity",
}
FAIL_CODES = {"hardware_error", "busy", "internal"}


class CommandResult(Wire):
    v: Version
    id: uuid.UUID
    device: uuid.UUID
    status: Literal["received", "applied", "rejected", "expired", "failed"]
    ts: TimestampMs
    code: Annotated[str, StringConstraints(pattern=r"^[a-z_]{1,32}$")] | None = None
    message: Annotated[str, StringConstraints(max_length=160)] | None = None
    revision: Annotated[int, Field(ge=0)] | None = None

    @model_validator(mode="after")
    def _codes(self) -> Self:
        if self.status == "rejected" and self.code not in REJECT_CODES:
            raise ValueError("rejected needs a reject code")
        if self.status == "failed" and self.code not in FAIL_CODES:
            raise ValueError("failed needs a failure code")
        if self.status in ("received", "applied", "expired") and self.code is not None:
            raise ValueError(f"{self.status} takes no code")
        return self


# --- state and config (§5) -------------------------------------------------------------------


class LightOutput(Wire):
    power: bool
    brightness: Percent | None = None
    red: Byte | None = None
    green: Byte | None = None
    blue: Byte | None = None
    white: Byte | None = None
    color_temperature: Kelvin | None = None


class LightState(Wire):
    id: LocalId
    type: LightType
    available: bool
    output: LightOutput
    effect: Annotated[str, StringConstraints(max_length=24)] | None = None


class InputState(Wire):
    id: LocalId
    state: bool


class State(Wire):
    v: Version
    device: uuid.UUID
    ts: TimestampMs
    rev: Annotated[int, Field(ge=0)]
    lights: Annotated[list[LightState], Field(max_length=64)]
    inputs: Annotated[list[InputState], Field(max_length=64)] | None = None


class Supports(Wire):
    brightness: bool
    color: bool
    white: bool
    color_temperature: bool
    kelvin_min: Kelvin | None = None
    kelvin_max: Kelvin | None = None
    effects: Annotated[list[Annotated[str, StringConstraints(max_length=24)]], Field(max_length=32)]


class LightConfig(Wire):
    id: LocalId
    name: Name
    type: LightType
    room_id: LocalId | None = None
    supports: Supports


class Named(Wire):
    id: LocalId
    name: Name
    members: Annotated[list[LocalId], Field(max_length=64)] | None = None


class AutomationConfig(Wire):
    id: LocalId
    name: Name
    enabled: bool


class Caps(Wire):
    ota: bool | None = None
    backup: bool | None = None


class ConfigReported(Wire):
    v: Version
    device: uuid.UUID
    rev: Annotated[int, Field(ge=0)]
    ts: TimestampMs
    fw: SemVer
    board: Annotated[str, StringConstraints(min_length=1, max_length=48)]
    controller_id: Annotated[str, StringConstraints(max_length=32)] | None = None
    ip: Annotated[str, StringConstraints(pattern=r"^(\d{1,3}\.){3}\d{1,3}$")] | None = None
    caps: Caps
    lights: Annotated[list[LightConfig], Field(max_length=64)]
    rooms: Annotated[list[Named], Field(max_length=64)]
    groups: Annotated[list[Named], Field(max_length=64)]
    scenes: Annotated[list[Named], Field(max_length=64)]
    automations: Annotated[list[AutomationConfig], Field(max_length=64)]


# --- gateway assignment (§2) ---------------------------------------------------------------------


class Child(Wire):
    device: uuid.UUID
    route_epoch: Annotated[int, Field(ge=1)]
    controller_id: Annotated[str, StringConstraints(pattern=r"^[a-z0-9-]{1,32}$")] | None = None


class Children(Wire):
    v: Version
    gateway: uuid.UUID
    ts: TimestampMs
    children: Annotated[list[Child], Field(max_length=64)]

    @model_validator(mode="after")
    def _unique(self) -> Self:
        if len({c.device for c in self.children}) != len(self.children):
            raise ValueError("duplicate child")
        return self
