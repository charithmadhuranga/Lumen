# P5 — Firmware: local tickets, gateway pairing, Cloud card

Skills: `lumen-networking`, `lumen-security-ota`, `lumen-webui`.

Under DECISIONS D-17, controllers never connect to the cloud, so this phase
adds no MQTT client, no TLS session and no HTTPS call. `MqttService` is not
modified (brief §2.2), and all firmware rules in AGENTS.md apply.

## Outcome

A controller can be adopted by a Lumen Gateway. After that:

- it accepts local tickets from the gateway and from app members;
- the gateway (and through it the cloud) controls it with the same local
  API that the web UI uses;
- its local UI shows which gateway and home it belongs to.

With `cloud.mode = off`, which is the default, it behaves exactly like
0.1.0.

## Tasks

### 5.1 Settings and secrets

- [ ] `config/cloud.json` (`schemas/cloud.schema.json`, version 1; a
      missing file means `off`, so no migration risk):
  - `mode` (`off`/`gateway`),
  - `deviceId`, `gatewayId`, `gatewayUrl`,
  - `ticketEpoch`,
  - `pairedAt`.
- [ ] NVS namespace `cloud`: `local_key` (32 B) and `last_time`.
  - These are never in exports, logs or backups.
  - Factory reset erases them.
  - The existing backup code must not see them (host test).

### 5.2 Local tickets (PROTOCOL §6)

- [ ] `src/web/ticket_auth.cpp`: a pure, host-testable verifier.
  - Uses the check order from PROTOCOL §6, an mbedTLS HMAC-SHA256, a
    constant-time compare, and no heap beyond a 512-byte stack buffer.
  - Its clock source is the SNTP time when synchronised, otherwise the
    `last_time` from NVS.
- [ ] `web_server.cpp`: a request carrying
      `Authorization: Lumen-Ticket …` is authenticated by the ticket, with
      no CSRF check. The WebSocket upgrade accepts `?ticket=` as well.
      Rejections return 401 with the reason code, and are counted in
      diagnostics.
- [ ] Route auth gains `RouteAuth::kControl`, alongside the existing
      `kPublic`, `kPasswordChange` and `kUser`:
  - **Control tickets** can reach the light, group, room and scene
    commands and reads, `/system/info`, `/lights/effects` and the WS.
  - **Admin tickets** can reach everything a signed-in user can, except
    `/auth/password`, `/system/factory-reset`, `/cloud/*` and the
    integration and ESP-NOW key routes. Those need a session.
  - A table-driven host test covers every route.
- [ ] `last_time` is persisted every 10 min and at shutdown (housekeeping).

### 5.3 Pairing with a gateway

- [ ] `POST /api/v1/cloud/pair` (session only):
  - body `{claimToken, gatewayUrl}`;
  - validated, then the pairing request is enqueued;
  - the housekeeping task calls `POST {gatewayUrl}/local/v1/register`
    (`esp_http_client`, plain HTTP on the LAN, 5 s timeout, response at
    most 1 KB, validated against `register-response`);
  - it stores `deviceId`, `gatewayId`, `ticketEpoch` and `K_local`;
  - mode becomes `gateway`.
- [ ] `GET /api/v1/cloud`:
  - mode, state (`off`, `pairing`, `paired`, `pair_failed` with a
    reason), `deviceId`, `gatewayId`, `gatewayUrl`;
  - when a `g:` ticket was last seen;
  - ticket counters.
- [ ] `POST /api/v1/cloud/unpair` (session): clears the secrets and the
      settings. The gateway is told, best effort; the cloud is the
      authority for removal.
- [ ] `PUT /api/v1/cloud/epoch`: `{ticketEpoch}` may only increase. It is
      reachable by a `g:` admin ticket; the gateway forwards
      `config.apply` kind `cloud`.
- [ ] Discovery: the UDP announce and mDNS TXT records gain `cloud=off|paired`
      and the `deviceId` when paired, so the gateway and the app can
      list adoptable controllers.

### 5.4 Revision counter (needed by P8, cheap now)

- [ ] A persistent `configRevision`, raised on every successful
      `ConfigManager` save from any source. It is exposed in
      `GET /system/info` and in `config_changed` WS events.

### 5.5 Local web UI

- [ ] Integrations → **Lumen Cloud** card:
  - state, home/gateway, last contact;
  - Pair (paste a claim code from the app) and Unpair, the latter
    confirmed with a dialog;
  - explanation: "Remote access goes through your Lumen Gateway. Lights
    and automations keep working without it."

  Zod schemas, tests, and the bundle budget respected.

## Tests

- **Host:**
  - the `protocol/fixtures/tickets.json` vectors, with the same verdict
    and reason as `ticket_ref.py`;
  - the route matrix per scope;
  - CSRF still enforced for session requests;
  - the clock fallback;
  - the `cloud.json` schema and the absent-file default;
  - the pairing response validator against the `register-response`
    fixtures;
  - secrets absent from backup output.
- **Device, KC868-AP:**
  - pair with the P6 gateway (or, first, with a fake gateway on this
    PC);
  - drive lights with tickets from `test/device/` scripts;
  - an expired, tampered or old-epoch ticket gets 401;
  - reboot without internet: the ticket is still accepted;
  - the existing suites (e2e, HA, security, perf) pass unchanged with
    `mode=off` and with `mode=gateway`.
- **Memory:** the flash delta and heap delta are recorded. Expected cost:
  a few KB of flash, about 1 KB of heap.

## Gate

The host and device tests above pass, and the memory figures are recorded
in STATUS.
