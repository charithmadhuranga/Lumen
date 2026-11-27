#!/usr/bin/env bash
# Development only: write EMQX's bootstrap API key file from .env, so the
# cloud can call the EMQX management API (disconnect revoked gateways).
# Output: dev-certs/emqx_api_keys.txt (git-ignored, mounted at /certs).
set -euo pipefail
cd "$(dirname "$0")/.."
secret="$(grep -E '^LUMEN_EMQX_API_SECRET=' .env | head -1 | cut -d= -f2-)"
if [ -z "$secret" ]; then
  echo "LUMEN_EMQX_API_SECRET is missing from .env (see .env.example)" >&2
  exit 1
fi
mkdir -p dev-certs
printf 'lumen-cloud:%s:administrator\n' "$secret" > dev-certs/emqx_api_keys.txt
chmod 644 dev-certs/emqx_api_keys.txt
