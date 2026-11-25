# Lumen integration test stack (Mosquitto + Home Assistant)

Tests the real controller end to end against a real MQTT broker and a real
Home Assistant, plus the local control interfaces (HTTP REST, TCP, UDP).
Hardware effects are checked by PCA9685/relay register read-back.

| Service | Image (pinned by digest) | Host port |
|---|---|---|
| Mosquitto | `eclipse-mosquitto:2.1.4` | 18830 |
| Home Assistant | `lscr.io/linuxserver/homeassistant:2026.11.2-ls267` | 8124 → http://localhost:8124 |

The ports avoid a developer's own broker (1883) and Home Assistant (8123).

## Run

```bash
cd test/integration/ha
./setup.sh                                  # first run: ~15 min (HA fixes package permissions once)
python3 ha_test.py http://<controller-ip> '<admin password>'
python3 ha_test.py http://<controller-ip> '<pw>' --only ha,udp    # groups: mqtt ha http tcp udp
python3 ha_test.py http://<controller-ip> '<pw>' -k restart       # name filter
```

`setup.sh` generates random broker passwords (`.env`, mode 600), hashes them
into `mosquitto/passwd`, starts the stack, onboards Home Assistant (owner
`lumen-test`) and adds the MQTT integration through its config flow. It is
idempotent. `ha_test.py` configures the controller's MQTT settings itself
(broker = this PC's LAN address towards the controller, override with
`LUMEN_BROKER_HOST`), creates `it_group` / `it_scene` and uses PCA9685
channels 0–5 and 12 plus `relay1`.

Generated state (`.env`, `.ha_token`, `ha-config/`, `mosquitto/passwd`,
`mosquitto/data/`) is gitignored. Clean slate:
`docker compose down && rm -rf ha-config mosquitto/data mosquitto/passwd .env .ha_token`.

## Coverage (34 tests)

- **mqtt** — status/LWT, retained state, set topic → hardware, invalid payloads rejected, group and scene topics, burst coalescing, diagnostics.
- **ha** — discovery (colour modes, effects, kelvin range), entity ids and device, brightness / RGB / colour temperature / relay / effect / transition round trips, changes made elsewhere, diagnostic sensors, create → rename → delete lifecycle, availability on MQTT disable, controller restart (state restored), HA restart, broker outage and recovery.
- **http** — light, room and group commands with validation, 401 without session, 403 without/with a wrong CSRF token, secrets never returned.
- **tcp** — full command set, state push, unauthenticated / wrong-token / oversized-line / 5th-client rejection, 10 s auth timeout, port closed when disabled.
- **udp** — unicast and broadcast discovery, signed light/group/scene control, replay / older sequence / bad MAC / stale or future timestamp / unsigned / tampered frames dropped, 20 frames/s limit, state announcements.
