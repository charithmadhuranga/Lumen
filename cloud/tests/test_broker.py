"""Live broker tests against the compose stack (`make up`): EMQX asks the API
container to authenticate and authorise every client. Skipped when it is not up.

These go through the running API container (dev database), not the in-process
test app, because EMQX calls the container's hooks.
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import os
import socket
import ssl
import urllib.error
import urllib.request
import uuid
from typing import Any

import aiomqtt
import pytest

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CA = os.path.join(HERE, "dev-certs", "ca.pem")
HOST, TLS_PORT, API = "127.0.0.1", 28883, "http://127.0.0.1:8800/api/v1"
SERVICE_PASSWORD = "dev-insecure-change-me-0123456789abcdef"  # .env.example development value


def _up() -> bool:
    try:
        with socket.create_connection((HOST, TLS_PORT), timeout=1), socket.create_connection((HOST, 8800), timeout=1):
            return os.path.exists(CA)
    except OSError:
        return False


pytestmark = pytest.mark.skipif(not _up(), reason="compose stack not running (make up)")


def _tls() -> ssl.SSLContext:
    return ssl.create_default_context(cafile=CA)


def _call(method: str, path: str, body: Any = None, token: str | None = None) -> tuple[int, Any]:
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(  # noqa: S310 - fixed local URL
        API + path, method=method, data=json.dumps(body).encode() if body is not None else None, headers=headers
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as r:  # noqa: S310
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, json.load(e)


def _gateway() -> tuple[str, str, str]:
    """(access token, home id, gateway id/secret...) for a fresh gateway in a fresh home."""
    email, pw = "live-broker@example.com", "live broker phrase 1"
    status, body = _call("POST", "/auth/login", {"email": email, "password": pw})
    if status != 200:
        status, body = _call("POST", "/auth/register", {"email": email, "password": pw})
        assert status == 201, body
    token = body["data"]["accessToken"]
    _, home = _call("POST", "/homes", {"name": f"Live {uuid.uuid4().hex[:6]}", "timezone": "UTC"}, token)
    home_id = home["data"]["id"]
    code = "K7QM2XRA"
    _, claim = _call(
        "POST", f"/homes/{home_id}/gateway-claims", {"setupProofHash": hashlib.sha256(code.encode()).hexdigest()}, token
    )
    mac = "d8:3a:dd:" + ":".join(f"{b:02x}" for b in os.urandom(3))
    status, reg = _call(
        "POST",
        "/gateway/register",
        {
            "v": 1,
            "claimToken": claim["data"]["claimToken"],
            "setupProof": code,
            "hw": {"model": "test-gw", "mac": mac},
            "version": "1.0.0",
        },
    )
    assert status == 201, reg
    return token, reg["data"]["gatewayId"], reg["data"]["mqtt"]["password"]


async def test_unknown_and_anonymous_clients_are_refused() -> None:
    for kwargs in (
        {"username": f"g:{uuid.uuid4()}", "password": "x" * 43, "identifier": f"g:{uuid.uuid4()}"},
        {"identifier": "anon-test"},
    ):
        with pytest.raises(aiomqtt.MqttError):
            async with aiomqtt.Client(HOST, TLS_PORT, tls_context=_tls(), timeout=5, **kwargs):  # type: ignore[arg-type]
                pass


def test_certificate_is_verified() -> None:
    ctx = ssl.create_default_context()
    with socket.create_connection((HOST, TLS_PORT), timeout=3) as raw, pytest.raises(ssl.SSLCertVerificationError):
        ctx.wrap_socket(raw, server_hostname="localhost")


async def test_gateway_lifecycle_on_the_real_broker() -> None:
    token, gw, secret = _gateway()
    cid = f"g:{gw}"
    stranger = uuid.uuid4()
    received: list[str] = []

    async with aiomqtt.Client(
        HOST,
        TLS_PORT,
        identifier="svc:live-test",
        username="svc:live-test",
        password=SERVICE_PASSWORD,
        tls_context=_tls(),
        timeout=5,
    ) as svc:
        await svc.subscribe("lumen/v1/#", qos=1)

        async def collect() -> None:
            async for message in svc.messages:
                received.append(str(message.topic))

        collector = asyncio.create_task(collect())
        async with aiomqtt.Client(
            HOST, TLS_PORT, identifier=cid, username=cid, password=secret, tls_context=_tls(), timeout=5
        ) as gateway:
            await gateway.subscribe(f"lumen/v1/g/{gw}/children", qos=1)
            await gateway.publish(f"lumen/v1/g/{gw}/status", b'{"v":1}', qos=1)  # allowed
            await gateway.publish(f"lumen/v1/d/{stranger}/state", b"{}", qos=1)  # not its child: dropped
            await gateway.publish(f"lumen/v1/g/{gw}/children", b"{}", qos=1)  # only the cloud assigns: dropped
            await asyncio.sleep(1.0)
            assert f"lumen/v1/g/{gw}/status" in received
            assert f"lumen/v1/d/{stranger}/state" not in received
            assert received.count(f"lumen/v1/g/{gw}/children") == 0

            # Releasing the gateway in the app disconnects it at once and refuses reconnection.
            status, _ = _call("DELETE", f"/devices/{gw}", token=token)
            assert status == 200

            async def keep_publishing() -> None:
                for _ in range(50):
                    await gateway.publish(f"lumen/v1/g/{gw}/status", b"{}", qos=1)
                    await asyncio.sleep(0.1)

            with pytest.raises(aiomqtt.MqttError):
                await keep_publishing()  # the broker drops the connection within ~5 s
        collector.cancel()

    with pytest.raises(aiomqtt.MqttError):
        async with aiomqtt.Client(
            HOST, TLS_PORT, identifier=cid, username=cid, password=secret, tls_context=_tls(), timeout=5
        ):
            pass
