"""Minimal Lumen API client for on-device integration tests.

Handles the session cookie + X-CSRF-Token header and unwraps the
{success, data, error} envelope. Used by test/device/test_api.py.
"""
import json
import os

import requests


class ApiError(Exception):
    def __init__(self, status, code, message, data=None):
        super().__init__(f"{status} {code}: {message}")
        self.status, self.code, self.message, self.data = status, code, message, data


class Lumen:
    def __init__(self, base=None, timeout=15):
        self.base = (base or os.environ.get("LUMEN_URL", "http://lumen.local")).rstrip("/")
        self.s = requests.Session()
        self.csrf = None
        self.timeout = timeout

    def _req(self, method, path, body=None, raw=False):
        headers = {}
        if method != "GET" and self.csrf:
            headers["X-CSRF-Token"] = self.csrf
        r = self.s.request(method, self.base + "/api/v1" + path,
                           data=None if body is None else json.dumps(body),
                           headers=headers, timeout=self.timeout)
        if raw:
            return r
        try:
            env = r.json()
        except ValueError:
            raise ApiError(r.status_code, "NOT_JSON", r.text[:200])
        if not env.get("success"):
            err = env.get("error") or {}
            raise ApiError(r.status_code, err.get("code"), err.get("message"), env.get("data"))
        return env.get("data")

    def get(self, p): return self._req("GET", p)
    def post(self, p, b=None): return self._req("POST", p, b if b is not None else {})
    def put(self, p, b): return self._req("PUT", p, b)
    def delete(self, p): return self._req("DELETE", p)

    def login(self, user, password):
        d = self.post("/auth/login", {"username": user, "password": password})
        self.csrf = d["csrfToken"]
        return d
