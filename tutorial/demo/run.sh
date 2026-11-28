#!/usr/bin/env bash
# Run a demo_build.py step with the device + Home Assistant env loaded.
#   ./run.sh record overview        ./run.sh overlays        ./run.sh all
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
set -a
. "$HERE/.secrets.env"
[ -f "$REPO/test/integration/ha/.env" ] && . "$REPO/test/integration/ha/.env"
set +a
export DEMO_NODE_MODULES="${DEMO_NODE_MODULES:-$REPO/webui}"
cd "$HERE/.."
exec .venv/bin/python demo/video/demo_build.py "$@"
