#!/usr/bin/env python3
"""Generate docs/pro/openapi.json and the endpoint table in docs/pro/CLOUD_API.md.

    uv run python tools/openapi.py           write both
    uv run python tools/openapi.py --check   CI: fail if either is stale

No database is needed: the app is built but never started.
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from app.core.settings import Settings
from app.main import create_public_app

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPEC = os.path.join(ROOT, "docs", "pro", "openapi.json")
DOC = os.path.join(ROOT, "docs", "pro", "CLOUD_API.md")
BEGIN, END = "<!-- BEGIN GENERATED ENDPOINTS -->", "<!-- END GENERATED ENDPOINTS -->"
METHODS = ("get", "post", "put", "patch", "delete")


PUBLIC = {"/healthz", "/readyz"} | {
    f"/api/v1/auth/{p}"
    for p in ("register", "login", "refresh", "verify-email", "password-reset/request", "password-reset/confirm")
}
GATEWAY = {"/api/v1/gateway/children", "/api/v1/gateways/{gateway_id}/children/register"}


def auth_of(path: str) -> str:
    if path in PUBLIC:
        return "none"
    if path == "/api/v1/gateway/register":
        return "claim token + setup code"
    return "gateway (Basic)" if path in GATEWAY else "Bearer"


def build() -> tuple[str, str]:
    app = create_public_app(Settings(environment="development"))
    spec = app.openapi()
    spec_text = json.dumps(spec, indent=2, sort_keys=True) + "\n"
    rows = ["| Method | Path | Summary | Auth |", "|---|---|---|---|"]
    for path in sorted(spec["paths"]):
        for method in METHODS:
            op = spec["paths"][path].get(method)
            if op is not None:
                rows.append(f"| {method.upper()} | `{path}` | {op.get('summary', '')} | {auth_of(path)} |")
    return spec_text, "\n".join(rows)


def main() -> None:
    spec_text, table = build()
    with open(DOC, encoding="utf-8") as f:
        doc = f.read()
    head, rest = doc.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    new_doc = f"{head}{BEGIN}\n{table}\n{END}{tail}"
    if "--check" in sys.argv:
        current_spec = ""
        if os.path.exists(SPEC):
            with open(SPEC, encoding="utf-8") as f:
                current_spec = f.read()
        if current_spec != spec_text or doc != new_doc:
            sys.exit("docs/pro/openapi.json or CLOUD_API.md is stale: run uv run python tools/openapi.py")
        print("API docs up to date")
        return
    with open(SPEC, "w", encoding="utf-8") as f:
        f.write(spec_text)
    with open(DOC, "w", encoding="utf-8") as f:
        f.write(new_doc)
    print(f"wrote {SPEC} and the endpoint table in {DOC}")


if __name__ == "__main__":
    main()
