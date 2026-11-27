"""Contract: app/protocol.py accepts every valid and rejects every invalid protocol fixture."""

from __future__ import annotations

import json
import os

import pytest
from pydantic import BaseModel, ValidationError

from app import protocol

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXTURES = os.path.join(ROOT, "protocol", "fixtures")
MODELS: dict[str, type[BaseModel]] = {
    "command": protocol.Command,
    "command-result": protocol.CommandResult,
    "state": protocol.State,
    "config-reported": protocol.ConfigReported,
    "children": protocol.Children,
}


def cases(kind: str) -> list[tuple[str, str]]:
    return [
        (schema, os.path.join(FIXTURES, schema, kind, name))
        for schema in MODELS
        for name in sorted(os.listdir(os.path.join(FIXTURES, schema, kind)))
    ]


def load(path: str) -> object:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


@pytest.mark.parametrize(("schema", "path"), cases("valid"), ids=lambda x: os.path.relpath(str(x), FIXTURES))
def test_valid(schema: str, path: str) -> None:
    MODELS[schema].model_validate(load(path))


@pytest.mark.parametrize(("schema", "path"), cases("invalid"), ids=lambda x: os.path.relpath(str(x), FIXTURES))
def test_invalid(schema: str, path: str) -> None:
    with pytest.raises(ValidationError):
        MODELS[schema].model_validate(load(path))


def test_round_trip_keeps_wire_keys() -> None:
    raw = load(os.path.join(FIXTURES, "config-reported", "valid", "kc868.json"))
    parsed = protocol.ConfigReported.model_validate(raw)
    assert parsed.wire()["lights"][1]["supports"]["kelvinMin"] == 2700  # type: ignore[index]
    assert parsed.wire()["controllerId"] == "lumen-0798"
