#!/usr/bin/env bash
# Bring up the Lumen integration stack (Mosquitto + Home Assistant) and
# configure it unattended:
#   1. random broker passwords for users "lumen" and "homeassistant" (.env)
#   2. docker compose up
#   3. Home Assistant onboarding (owner "lumen-test") + MQTT integration
# Re-running is safe: existing passwords and onboarding are kept.
# Usage: ./setup.sh            (then: python3 ha_test.py <controller-url> <admin-password>)
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -f .env ]]; then
  umask 077
  gen() { python3 -c 'import secrets; print(secrets.token_urlsafe(18))'; }
  cat > .env <<EOF
MQTT_LUMEN_PASSWORD=$(gen)
MQTT_HA_PASSWORD=$(gen)
HA_OWNER_PASSWORD=$(gen)
EOF
fi
set -a; source .env; set +a

mkdir -p mosquitto/data ha-config
if [[ ! -s mosquitto/passwd ]]; then
  : > mosquitto/passwd
  # Hash with the pinned broker image itself (no local mosquitto needed).
  docker run --rm -v "$PWD/mosquitto:/m" --entrypoint sh \
    eclipse-mosquitto:2.1.4@sha256:fa12172a051310efe5bcb7a6a11b77bd5faacb82cbacd7742aa758bf024fd8e4 -c \
    "mosquitto_passwd -b /m/passwd lumen '$MQTT_LUMEN_PASSWORD' && \
     mosquitto_passwd -b /m/passwd homeassistant '$MQTT_HA_PASSWORD' && \
     chown 1883:1883 /m/passwd /m/data && chmod 0700 /m/passwd"
fi

docker compose up -d
python3 ha_setup.py
