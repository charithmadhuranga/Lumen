"""Cloud ticket implementation vs protocol/fixtures/tickets.json; broker ACL rules."""

from __future__ import annotations

import json
import os
import uuid

import pytest

from app.services import broker, tickets

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
with open(os.path.join(ROOT, "protocol", "fixtures", "tickets.json"), encoding="utf-8") as _f:
    VECTORS = json.load(_f)
KEY = bytes.fromhex(VECTORS["key"])


@pytest.mark.parametrize("case", VECTORS["valid"], ids=lambda c: c["name"])
def test_valid_vectors_verify_and_mint_identically(case: dict[str, object]) -> None:
    got = tickets.verify(str(case["ticket"]), KEY, VECTORS["device"], VECTORS["epoch"], VECTORS["now"])
    assert got == tickets.Claims(**case["claims"])  # type: ignore[arg-type]
    assert tickets.mint(KEY, got) == case["ticket"]


@pytest.mark.parametrize("case", VECTORS["invalid"], ids=lambda c: c["name"])
def test_invalid_vectors_fail_with_the_same_reason(case: dict[str, str]) -> None:
    with pytest.raises(tickets.TicketError) as err:
        tickets.verify(case["ticket"], KEY, VECTORS["device"], VECTORS["epoch"], VECTORS["now"])
    assert err.value.reason == case["reason"]


def test_mint_refuses_lifetimes_outside_protocol() -> None:
    dev = VECTORS["device"]
    with pytest.raises(ValueError, match="lifetime"):
        tickets.mint(KEY, tickets.Claims(f"u:{dev}", dev, "admin", 100, 100 + 12 * 3600 + 1, 0))
    with pytest.raises(ValueError, match="lifetime"):
        tickets.mint(KEY, tickets.Claims(f"u:{dev}", dev, "root", 100, 200, 0))


# --- broker rules ----------------------------------------------------------------------

GW = uuid.UUID("0193f6c2-7a10-7c00-8a00-00000000aa01")
OTHER_GW = uuid.UUID("0193f6c2-7a10-7c00-8a00-00000000aa02")
CHILD = uuid.UUID("0193f6c2-7a10-7b44-9d92-001122334455")
STRANGER = uuid.UUID("0193f6c2-7a10-7b44-9d92-0011223344ff")


@pytest.mark.parametrize(
    ("clientid", "username", "expected"),
    [
        (f"g:{GW}", f"g:{GW}", broker.Client("gateway", GW)),
        (f"g:{GW}", f"g:{OTHER_GW}", None),  # username must equal client id
        (f"g:{str(GW).upper()}", f"g:{str(GW).upper()}", None),  # canonical lower-case only
        ("g:not-a-uuid", "g:not-a-uuid", None),
        (f"d:{CHILD}", f"d:{CHILD}", None),  # controllers never connect (D-17)
        ("svc:bridge-1", "svc:bridge-1", broker.Client("service")),
        ("svc:Bad_Name", "svc:Bad_Name", None),
        ("", "", None),
        ("anonymous", "", None),
    ],
)
def test_parse_client(clientid: str, username: str, expected: broker.Client | None) -> None:
    assert broker.parse_client(clientid, username) == expected


D, G = f"lumen/v1/d/{CHILD}", f"lumen/v1/g/{GW}"
GATEWAY_CASES = [
    # own gateway topics
    (f"{G}/availability", "publish", True),
    (f"{G}/status", "publish", True),
    (f"{G}/children", "subscribe", True),
    (f"{G}/children", "publish", False),  # only the cloud assigns children
    (f"{G}/availability", "subscribe", False),
    (f"lumen/v1/g/{OTHER_GW}/status", "publish", False),
    (f"lumen/v1/g/{OTHER_GW}/children", "subscribe", False),
    # assigned child
    (f"{D}/state", "publish", True),
    (f"{D}/availability", "publish", True),
    (f"{D}/cmd/result", "publish", True),
    (f"{D}/config/reported", "publish", True),
    (f"{D}/events", "publish", True),
    (f"{D}/telemetry", "publish", True),
    (f"{D}/cmd", "subscribe", True),
    (f"{D}/cmd", "publish", False),  # commands come only from the cloud
    (f"{D}/state", "subscribe", False),
    # not a child
    (f"lumen/v1/d/{STRANGER}/state", "publish", False),
    (f"lumen/v1/d/{STRANGER}/cmd", "subscribe", False),
    # wildcards, other namespaces, malformed
    ("lumen/v1/d/+/cmd", "subscribe", False),
    ("lumen/v1/#", "subscribe", False),
    (f"$share/x/{D}/cmd", "subscribe", False),
    ("$SYS/brokers", "subscribe", False),
    (f"homeassistant/light/{CHILD}/config", "publish", False),
    (f"{D}/state/extra", "publish", False),
    (f"lumen/v2/d/{CHILD}/state", "publish", False),
]


@pytest.mark.parametrize(("topic", "action", "allowed"), GATEWAY_CASES)
def test_gateway_acl(topic: str, action: broker.Action, allowed: bool) -> None:
    assert broker.gateway_allowed(GW, {CHILD}, topic, action) is allowed


@pytest.mark.parametrize(
    ("topic", "action", "allowed"),
    [
        (f"{D}/cmd", "publish", True),
        (f"{G}/children", "publish", True),
        (f"{D}/state", "publish", False),
        (f"{G}/availability", "publish", False),
        ("lumen/v1/d/+/state", "subscribe", True),
        ("$share/lumen/lumen/v1/d/+/state", "subscribe", True),
        ("$share/lumen", "subscribe", False),
        ("#", "subscribe", False),
        ("homeassistant/#", "subscribe", False),
    ],
)
def test_service_acl(topic: str, action: broker.Action, allowed: bool) -> None:
    assert broker.service_allowed(topic, action) is allowed


# --- contract: the cloud's request models agree with protocol/fixtures ----------------------

from pydantic import ValidationError  # noqa: E402

from app.api.schemas import ControllerRegister, GatewayRegister  # noqa: E402

FIXTURES = os.path.join(ROOT, "protocol", "fixtures")
MODELS = {"register-request": ControllerRegister, "gateway-register-request": GatewayRegister}


def _cases(kind: str) -> list[tuple[str, str]]:
    return [
        (schema, os.path.join(FIXTURES, schema, kind, f))
        for schema in MODELS
        for f in sorted(os.listdir(os.path.join(FIXTURES, schema, kind)))
    ]


@pytest.mark.parametrize(("schema", "path"), _cases("valid"), ids=lambda x: os.path.basename(str(x)))
def test_cloud_models_accept_valid_fixtures(schema: str, path: str) -> None:
    with open(path, encoding="utf-8") as f:
        MODELS[schema].model_validate(json.load(f))


@pytest.mark.parametrize(("schema", "path"), _cases("invalid"), ids=lambda x: os.path.basename(str(x)))
def test_cloud_models_reject_invalid_fixtures(schema: str, path: str) -> None:
    with open(path, encoding="utf-8") as f, pytest.raises(ValidationError):
        MODELS[schema].model_validate(json.load(f))
