#!/usr/bin/env bash
# Development-only TLS material for the local broker (never used in production).
#
#   tools/dev_certs.sh [extra-SAN ...]      e.g. tools/dev_certs.sh 192.168.1.20 lumen-dev.local
#
# Writes dev-certs/{ca.pem,ca.key,server.pem,server.key} (git-ignored). The CA
# is what a development gateway trusts (P6: --ca dev-certs/ca.pem). Re-running
# keeps an existing CA so already-configured clients stay valid.
set -euo pipefail
cd "$(dirname "$0")/.."
out=dev-certs
mkdir -p "$out"
chmod 700 "$out"
if [ ! -f "$out/ca.key" ]; then
  openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 825 \
    -subj "/CN=Lumen Dev CA" -keyout "$out/ca.key" -out "$out/ca.pem" 2>/dev/null
fi
san="DNS:localhost,DNS:emqx,IP:127.0.0.1"
for extra in "$@"; do
  if [[ "$extra" =~ ^[0-9.]+$ ]]; then san="$san,IP:$extra"; else san="$san,DNS:$extra"; fi
done
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -subj "/CN=lumen-dev-broker" \
  -keyout "$out/server.key" -out "$out/server.csr" 2>/dev/null
openssl x509 -req -in "$out/server.csr" -CA "$out/ca.pem" -CAkey "$out/ca.key" -CAcreateserial \
  -days 365 -out "$out/server.pem" \
  -extfile <(printf "subjectAltName=%s\nextendedKeyUsage=serverAuth\n" "$san") 2>/dev/null
rm -f "$out/server.csr"
chmod 600 "$out"/*.key
# EMQX runs as uid 1000 inside its container and must read the key.
chmod 644 "$out/server.key" "$out/server.pem" "$out/ca.pem"
echo "dev certificates in $out/ (SAN: $san)"
