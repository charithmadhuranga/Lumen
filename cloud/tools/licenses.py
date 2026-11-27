#!/usr/bin/env python3
"""Write cloud/licenses.json: name, version and licence of every production dependency.

    uv run python tools/licenses.py           (run after changing dependencies)
    uv run python tools/licenses.py --check   (CI: fail if stale)

Read by scripts/third_party_notices.mjs. The production set comes from
`uv export --no-dev`; licences from installed package metadata.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from importlib import metadata

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "licenses.json")


def licence_of(dist: metadata.Distribution) -> str:
    meta = dist.metadata
    expr = meta.get("License-Expression")
    if expr:
        return expr.strip()
    classifiers = [c.split("::")[-1].strip() for c in meta.get_all("Classifier") or [] if c.startswith("License ::")]
    mapping = {
        "MIT License": "MIT",
        "BSD License": "BSD-3-Clause",
        "Apache Software License": "Apache-2.0",
        "ISC License (ISCL)": "ISC",
        "Python Software Foundation License": "PSF-2.0",
        "Eclipse Public License 2.0 (EPL-2.0)": "EPL-2.0 OR EDL-1.0",
        "Mozilla Public License 2.0 (MPL 2.0)": "MPL-2.0",
    }
    names = sorted({mapping.get(c, c) for c in classifiers})
    if names:
        return " OR ".join(names)
    raw = (meta.get("License") or "").strip().splitlines()
    return raw[0][:60] if raw else "UNKNOWN"


def main() -> None:
    exported = subprocess.run(
        ["uv", "export", "--no-dev", "--no-hashes", "--frozen", "--format", "requirements-txt"],  # noqa: S607
        cwd=HERE,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    names = sorted({m.group(1).lower() for m in re.finditer(r"^([A-Za-z0-9_.\-]+)==", exported, re.M)})
    rows = []
    for name in names:
        dist = metadata.distribution(name)
        rows.append(
            {
                "name": dist.metadata["Name"],
                "version": dist.version,
                "license": licence_of(dist),
                "url": f"https://pypi.org/project/{dist.metadata['Name']}/",
            }
        )
    text = json.dumps(rows, indent=2) + "\n"
    if "--check" in sys.argv:
        current = ""
        if os.path.exists(OUT):
            with open(OUT, encoding="utf-8") as f:
                current = f.read()
        if current != text:
            sys.exit("cloud/licenses.json is out of date: run uv run python tools/licenses.py")
        print("cloud/licenses.json up to date")
        return
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {OUT} ({len(rows)} packages)")


if __name__ == "__main__":
    main()
