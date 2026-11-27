# P3 — Devices, claiming, credentials, broker authorisation, local tickets

## Outcome

- A user can claim a (simulated) gateway, which then connects to EMQX
  with its own credential.
- The gateway can register controllers as its children, and the ACL hook
  allows exactly its own and its children's topics.
- Controllers never get broker credentials (DECISIONS D-17).
- The cloud mints local tickets that verify against `protocol/fixtures`.

## Tasks

- [x] Tables:
  - `devices`:
    - `kind` (`controller` / `gateway`),
    - `hw_model`, `board`, `mac`, `chip`,
    - `fw_version`, `status`, `controller_id`,
    - `home_id`, `room_id` (nullable),
    - `gateway_id`, `route_epoch`,
    - `ticket_epoch`,
    - `local_key_enc` (AES-256-GCM under `LUMEN_KEK`, with key ID),
    - `last_seen_at`.
  - `gateway_credentials`: gateway device, SHA-256 hash, created, revoked.
  - `claims`: token hash, user, home, kind (`gateway` / `controller`), gateway, setup-proof hash, expiry, `used_at`.
- [x] Claim flow (PROTOCOL §8):
  - `POST /homes/{home}/gateway-claims` and `POST /homes/{home}/claims`
    (a controller claim for a chosen gateway), admin or owner only;
  - `POST /gateway/register`, rate-limited by IP;
  - `POST /gateways/{gw}/children/register`, gateway-authenticated with a
    short-lived gateway JWT obtained with its MQTT credential via
    `POST /gateway/token`.

  Single use is enforced atomically (`UPDATE … WHERE used_at IS NULL`).
- [x] Gateway setup proof: the setup code (8 characters) is checked with
      a constant-time compare against the hash the app sends with the
      claim.
- [x] Device management:
  - `GET /homes/{home}/devices`, `GET /devices/{id}`, `PATCH` (name, room);
  - `DELETE` on a controller raises its ticket epoch, unassigns it and tells
    its gateway; on a gateway it revokes the credential and unassigns its
    children;
  - `GET /gateways/{gw}/children`; `POST` and `DELETE` for a child raise
    `route_epoch` and publish the retained `g/{gw}/children`.
- [x] `/internal/mqtt/auth` and `/internal/mqtt/acl` (PROTOCOL §7), on the
      internal port only, guarded by the hook secret. EMQX is configured
      to use them in compose. Revocation kicks the client through the EMQX
      management API.
- [x] `POST /devices/{id}/local-ticket`:
  - role member or above gives a `control` ticket for 7 days;
  - admin or owner can ask for an `admin` ticket for 12 h.

  The response includes LAN hints (last IP, `controller_id`) from
  `config/reported`.
- [x] `app/services/tickets.py` is the reference implementation of
      PROTOCOL §6, checked against `protocol/fixtures/tickets.json`.

## Tests

- **Claims:** expiry, reuse, wrong home or mode.
- **Gateway:** the wrong setup proof is rejected.
- **Broker hooks:** a parametrised ACL matrix covering:
  - a gateway on its own `g/` topics, in the allowed directions only,
  - a gateway on an assigned child (current epoch) versus a removed child,
  - foreign devices,
  - wildcard subscriptions refused.
- **Revocation:** a revoked credential is denied.
- **Live broker (compose):** a Python MQTT client using a registered
  gateway credential connects. A wrong password is refused. Publishing to
  a non-child's topic is refused (EMQX drops it or disconnects the
  client). Controller-style client IDs (`d:`) are always refused.
- **Tickets:** fixtures, scope by role, expiry limits, epoch.

## Gate

The tests pass, including the live-broker test against the compose stack
("local docker").

## Result (2026-11-27)

Gate passed. 262 cloud tests (111 new), ruff and mypy --strict clean, migration 0003 down/up and model parity.
Live broker tests against compose (EMQX 5.8.9 with HTTP authn/authz pointing at the API container): a gateway
registered through the API connects over TLS with its credential; its allowed publish is delivered; publishing
for a non-child or to its own `children` topic is dropped; releasing it in the API kicks it (EMQX management API)
and refuses reconnection; unknown and anonymous clients are refused (local docker).

Changes against the task list (all recorded in PROTOCOL.md §8 / CLOUD_API.md):
- Gateway REST authentication is HTTP Basic with the gateway's broker secret, not a separate gateway JWT
  (`POST /gateway/token` dropped): one credential, compared by SHA-256.
- No `POST/DELETE /gateways/{gw}/children/{device}`: moving a controller needs its gateway URL to change, so it is
  "remove, then pair again", and re-pairing reuses the device row (new K_local, epochs + 1).
- Publishing the retained `g/{gw}/children` and telling a gateway to forget a removed controller's K_local need
  the cloud's MQTT bridge, which is P4. Until then the broker ACL (live per request, 60 s cache) and
  `GET /gateway/children` are the source of truth.
- Added `GET /gateway/children` (state recovery for a reinstalled gateway, includes K_local) and the
  `gateway-register-request` protocol schema with fixtures; the cloud's request models are contract-tested
  against the protocol fixtures (this caught a setup-code rule mismatch, fixed).
- A wrong setup code burns the claim, so the 40-bit code cannot be brute-forced against one claim.

Limitations:
- EMQX `deny_action = ignore`: a forbidden publish is dropped silently, not disconnected (avoids flapping right
  after a controller is unassigned). Its ACL cache is 60 s, so a removed controller's topics can stay allowed for
  up to a minute; kicking the gateway is reserved for credential revocation.
- K_local travels from the cloud to the gateway over TLS, but from the gateway to the controller over plain LAN
  HTTP (P5/P6; key exchange in P11).
