"""The development broker (compose `emqx`) refuses every client until P3 adds the
cloud's auth/ACL hooks, and serves TLS only. Skipped when the broker is not up.
"""

from __future__ import annotations

import os
import socket
import ssl

import aiomqtt
import pytest

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CA = os.path.join(HERE, "dev-certs", "ca.pem")
HOST, TLS_PORT = "127.0.0.1", 28883


def _up() -> bool:
    try:
        with socket.create_connection((HOST, TLS_PORT), timeout=1):
            return os.path.exists(CA)
    except OSError:
        return False


pytestmark = pytest.mark.skipif(not _up(), reason="compose broker not running (make up)")


def _tls() -> ssl.SSLContext:
    ctx = ssl.create_default_context(cafile=CA)
    ctx.check_hostname = True
    return ctx


async def test_unknown_client_is_refused_over_tls() -> None:
    with pytest.raises(aiomqtt.MqttError):
        async with aiomqtt.Client(
            HOST, TLS_PORT, username="g:anyone", password="wrong", tls_context=_tls(), timeout=5, identifier="g:test"
        ):
            pass


async def test_anonymous_client_is_refused() -> None:
    with pytest.raises(aiomqtt.MqttError):
        async with aiomqtt.Client(HOST, TLS_PORT, tls_context=_tls(), timeout=5, identifier="anon-test"):
            pass


def test_certificate_is_verified() -> None:
    # A client that does not trust the dev CA must fail the handshake.
    ctx = ssl.create_default_context()
    with socket.create_connection((HOST, TLS_PORT), timeout=3) as raw, pytest.raises(ssl.SSLCertVerificationError):
        ctx.wrap_socket(raw, server_hostname="localhost")
