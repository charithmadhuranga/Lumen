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


def build() -> tuple[str, str]:
    app = create_public_app(Settings(environment="development"))
    spec = app.openapi()
    spec_text = json.dumps(spec, indent=2, sort_keys=True) + "\n"
    rows = ["| Method | Path | Summary | Auth |", "|---|---|---|---|"]
    for path in sorted(spec["paths"]):
        for method in METHODS:
            op = spec["paths"][path].get(method)
            if op is None:
                continue
            public = path in {"/healthz", "/readyz"} or path.split("/api/v1/auth/")[-1] in {
                "register",
                "login",
                "refresh",
                "verify-email",
                "password-reset/request",
                "password-reset/confirm",
            }
            rows.append(f"| {method.upper()} | `{path}` | {op.get('summary', '')} | {'none' if public else 'Bearer'} |")
    return spec_text, "\n".join(rows)


def main() -> None:
    spec_text, table = build()
    with open(DOC, encoding="utf-8") as f:
        doc = f.read()
    head, rest = doc.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    new_doc = f"{head}{BEGIN}\n{table}\n{END}{tail}"
    if "--check" in sys.argv:
        current_spec = open(SPEC, encoding="utf-8").read() if os.path.exists(SPEC) else ""  # noqa: SIM115
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
