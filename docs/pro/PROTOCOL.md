# Lumen Pro — Protocol Contract (v1)

The wire contract between the controller firmware, the gateway, Lumen Cloud
and the mobile app. Machine-readable schemas and example messages live in
[`protocol/`](../../protocol/): `schemas/*.schema.json` and
`fixtures/{valid,invalid}/`. Every implementation runs the fixtures in its
own test suite:

- firmware: host tests,
- cloud: pytest,
- gateway: `go test`,
- app: Vitest.

A change to this file without matching schema and fixture changes is
incomplete.

Design rules:

- Every message carries `"v": 1`. A receiver rejects any other major
  version and counts the rejection; it never guesses.
- All buffers are bounded. The MQTT payload limit is 4 KB for commands and
  results and 16 KB for state and config reports. Larger payloads are
  rejected before they are parsed.
- Times are Unix epoch **milliseconds**, as integers (`ts`, `exp`).
- IDs:
  - Cloud entities use UUIDv7 strings.
  - Controller-local IDs (lights, rooms, groups, scenes, automations) are
    the controller's own IDs (`[a-z0-9_-]{1,48}`, the firmware's `light_store::valid_id`).

## 1. Identities

| Name | Form | Assigned by |
|---|---|---|
| `device_id` | UUIDv7 | cloud, at claim |
| `gateway_id` | UUIDv7 (a gateway is a device of kind `gateway`) | cloud, at claim |
| `home_id`, `user_id` | UUIDv7 | cloud |
| `controller_id` | the controller's local name (`lumen-0798`) | firmware (metadata only) |
| MQTT client id | `g:{gateway_id}` / `svc:{instance}`. Controllers never connect to the broker (DECISIONS D-17) | — |

## 2. MQTT topics

The prefix is `lumen/v1`.

| Topic | Publisher → subscriber | QoS | Retained |
|---|---|---|---|
| `d/{device}/availability` | gateway → cloud (child's availability) | 1 | yes |
| `d/{device}/state` | gateway → cloud | 1 | yes |
| `d/{device}/cmd` | cloud → gateway | 1 | **never** |
| `d/{device}/cmd/result` | gateway → cloud | 1 | no |
| `d/{device}/config/reported` | gateway → cloud | 1 | yes |
| `d/{device}/events` | gateway → cloud | 1 | no |
| `d/{device}/telemetry` | gateway → cloud | 0 | no |
| `g/{gateway}/availability` | gateway → cloud (LWT) | 1 | yes |
| `g/{gateway}/children` | cloud → gateway (assignment list) | 1 | yes |
| `g/{gateway}/status` | gateway → cloud | 1 | yes |

**Authorisation** comes from the cloud's ACL hook (§7):

- A gateway can use `g/{self}/…`, plus `d/{child}/…` for each child whose
  current assignment is to this gateway at the current `route_epoch`.
- Service clients (the cloud itself) have the reverse directions.

**MQTT version and session:**

- Gateways use MQTT 5: message expiry on `cmd` equals the
  command's remaining TTL, and session expiry is 0, so a reconnect never
  delivers queued commands.

## 3. Commands — `d/{device}/cmd`

```json
{"v":1,"id":"0193f6c2-…","ts":1795780800000,"exp":1795780830000,
 "src":{"kind":"user","id":"0193…"},
 "op":"light.set","target":"living_main",
 "args":{"power":true,"brightness":75,"transitionMs":400}}
```

| `op` | `target` | `args` (all validated by range; same semantics as the local API, docs/API.md §3) |
|---|---|---|
| `light.set` | light id | `power`, `toggle`, `brightness` 0–100, `red/green/blue/white` 0–255, `colorTemperature` 1000–20000, `transitionMs` 0–600000 |
| `light.effect` | light id | `effect` (name or `stop`), `speedMs`, `durationMs` |
| `group.set` | group id | as `light.set` |
| `room.set` | room id | `power`, `brightness` |
| `scene.activate` | scene id | — |
| `device.status` | — | — (the device republishes `state` and `config/reported`) |
| `device.restart` | — | — (scope admin) |
| `config.apply` | document kind (`light`, `room`, `group`, `scene`, `automation`, `cloud`) | `{"baseRevision":N,"action":"upsert"\|"delete","id":"…","doc":{…}}`, see §5 |

**Gateway checks, in this order** (the gateway is the only MQTT receiver):

1. size,
2. JSON parse,
3. `v`,
4. `id` format,
5. `exp` (with the clock synchronised, `now > exp` gives `expired`; with no
   clock synchronisation, every command gives `clock_unsynced`),
6. the target device is an assigned child at the current `routeEpoch`,
7. `op` is known and the target exists,
8. argument ranges,
9. dedup: the IDs of the last 32 commands are kept, and a repeat returns
   the stored result again without executing.

Only then does the gateway call the controller's local API (§9). The
controller validates the request again with its own REST validators and
enqueues it on the **existing lighting command queue** with origin
`cloud:{src.id}`. Nothing on the controller is cloud-specific except the
ticket check.

## 4. Command results — `d/{device}/cmd/result`

```json
{"v":1,"id":"0193f6c2-…","device":"0193…","status":"applied","ts":1795780800412}
{"v":1,"id":"…","device":"…","status":"rejected","code":"unsupported_property","message":"light has no color"}
```

`status` is one of:

- `received`: optional, for slow operations,
- `applied`: the lighting engine accepted the command and wrote the output,
- `rejected`: carries one of these codes:
  - `invalid`,
  - `unknown_target`,
  - `unsupported_property`,
  - `forbidden`,
  - `conflict`,
  - `clock_unsynced`,
  - `capacity`,
  - `unsupported` (an op or config kind this firmware does not implement
    yet),
- `expired`,
- `failed`: `hardware_error`, `busy` or `internal`.

A `conflict` result carries `revision`, the controller's current config
revision.

The cloud keeps its own states on top of these: `queued` (stored) and
`sent` (published). The app shows success only on `applied` (brief §8.5).

## 5. State, config and availability

**State** (`d/{device}/state`, retained, coalesced: at most once every
250 ms per device, plus at least once every 5 min):

```json
{"v":1,"device":"…","ts":…,"rev":118,
 "lights":[{"id":"living_main","type":"rgb","available":true,
            "output":{"power":true,"brightness":75,"red":255,"green":180,"blue":90},
            "effect":null}],
 "inputs":[{"id":"in1","state":false}]}
```

- `output` is the **applied** state from the lighting engine, never the
  requested one.
- A gateway publishes a child's state on behalf of that child, from the
  child's local WebSocket.

**Config reported** (`d/{device}/config/reported`, retained):

```json
{"v":1,"device":"…","rev":42,"ts":…,
 "fw":"1.1.0","board":"kincony_kc868_ap","caps":{"ota":true},
 "lights":[{"id":"living_main","name":"Living ceiling","type":"rgb","roomId":"living",
            "supports":{"brightness":true,"color":true,"white":false,"colorTemperature":false,"effects":["fade","rainbow"]}}],
 "rooms":[…],"groups":[…],"scenes":[{"id":"evening","name":"Evening"}],"automations":[{"id":"…","name":"…","enabled":true}]}
```

- `rev` increases on every configuration save on the controller.
- `config.apply` with a `baseRevision` that differs from the current one
  is rejected with code `conflict` (D-07).

**Availability:**

- The payload is `online`, or `offline` as the Last Will.
- The gateway publishes each child's availability: `offline` when the
  child's WebSocket has been down for 15 s, `online` when it comes back.
- The broker's Last Will can only cover the gateway itself
  (`g/{gw}/availability`). When it fires, the cloud marks every child of
  that gateway offline; it does not wait for child topics.

## 6. Local tickets (controller local API)

The format is `LT1.<b64url(claims)>.<b64url(mac)>`, where
`mac = HMAC-SHA256(K_local, "LT1." + b64url(claims))`. `claims` is a
compact JSON object:

```json
{"sub":"u:0193…","dev":"0193…","scp":"control","iat":1795780800,"exp":1796385600,"ep":3}
```

- **`sub`:** `u:{user_id}` or `g:{gateway_id}`.
- **`scp`:** `control` or `admin`.
- **`iat` / `exp`:** Unix **seconds**.
- **`ep`:** epoch. The ticket is accepted only when `ep` equals the
  controller's `cloud.ticketEpoch`.
- **Clock handling:** `exp` is checked against wall-clock time if it is
  synchronised. If not, it is checked against the persisted last known time
  (saved every 10 min and at shutdown).
- **Lifetime:** `exp - iat` is at most 7 days for `control` and 12 h for
  `admin`.
- **Size:** at most 512 bytes in total.
- **Issued time:** `iat` may be at most 300 s in the future (clock skew).
- **Check order:** verifiers check in a fixed order, so every
  implementation reports the same reason for the same ticket:
  1. `format`
  2. `mac` (constant-time compare)
  3. `claims` (exactly these six keys, types, `scp`, the `sub` prefix)
  4. `device`
  5. `epoch`
  6. `lifetime`
  7. `not_yet_valid`
  8. `expired`

  The reference implementation is `protocol/tools/ticket_ref.py`. Its
  vectors are in `protocol/fixtures/tickets.json`.
- **MAC input:** verifiers compute the MAC over the received text and never
  re-serialise the claims.
- **Sent as:** the header `Authorization: Lumen-Ticket <ticket>` on REST.
  For the WebSocket, clients that cannot set headers can pass the
  `?ticket=` query parameter instead.

## 7. Broker auth hook (EMQX → cloud, internal network only)

Both endpoints are served only on the internal port and are guarded by
`X-Lumen-Hook-Secret`.

- `POST /internal/mqtt/auth` takes `{clientid, username, password}`.
  - It returns `{"result":"allow","is_superuser":false}` or
    `{"result":"deny"}`.
  - The username is the client ID. The password is the gateway's MQTT
    secret, compared against its SHA-256 hash in constant time.
  - A revoked or unknown credential is denied.
- `POST /internal/mqtt/acl` takes `{clientid, topic, action}`.
  - `action` is `publish` or `subscribe`.
  - It returns allow or deny per §2.
  - EMQX caches the answer for at most 60 s; revocation also kicks the
    client through the EMQX REST API.

## 8. Device registration and claiming (cloud REST, HTTPS)

| Step | Call | Notes |
|---|---|---|
| 1 | app → `POST /api/v1/homes/{home}/claims` `{gatewayId}` | → `{claimToken, expiresAt}`, single use, 15 min, bound to user + home + gateway. Format: `lct_` + 43 base64url characters (32 random bytes); the cloud stores only its SHA-256 |
| 2 | controller → gateway `POST /local/v1/register` `{claimToken, hw, fw, controllerId, url}`; the gateway → cloud `POST /api/v1/gateways/{gw}/children/register` (gateway-authenticated) | the cloud → gateway `{deviceId, localKey, ticketEpoch}`; the gateway → controller `{deviceId, localKey, ticketEpoch, gatewayId}` |
| 3 | app → `GET /api/v1/homes/{home}/devices` | the new device appears once its first `config/reported` arrives |
| gateway claim | app → `POST /api/v1/homes/{home}/gateway-claims` `{setupProofHash}` → `{claimToken}`; gateway → `POST /api/v1/gateway/register` `{claimToken, setupProof, hw, version}` | → `{gatewayId, mqtt:{url, username, password}}` (schema `gateway-register-response`, never logged). `setupProof` is the setup code shown on the gateway; the app sends its SHA-256 with the claim, the gateway sends the code itself |

**Release and re-claim:**

- `DELETE /api/v1/devices/{id}` (a controller) raises its ticket epoch
  (pushed through `config.apply` kind `cloud` first, if it is online),
  removes it from its gateway's children and tells the gateway to forget
  its `K_local`.
- `DELETE /api/v1/gateways/{id}` revokes the gateway's MQTT credential.
  Its controllers become unassigned. They keep working locally and can be
  adopted by another gateway.
- The controller clears its `cloud` NVS namespace when told to. A factory
  reset also clears it.

## 9. Gateway ↔ controller (LAN)

The gateway is a normal client of the controller's existing local API
(docs/API.md), authenticated with a `g:` ticket that it mints with
`K_local`.

- **Live state:** `GET /api/v1/ws`. The gateway maps
  `light_state_changed` to `state`, and `config_changed` to a refetch plus
  `config/reported`.
- **Commands:** `POST /api/v1/lights/{id}/command` and friends. The
  result is mapped from the HTTP status (202 gives `applied` once the next
  `light_state_changed` confirms it, or after 2 s; 4xx gives `rejected`
  with the mapped code).
- **Discovery:** UDP `LUMEN?` on port 8267, plus mDNS. A controller's IP
  address can change; the gateway follows it by `controller_id` and
  re-discovers it.
- **Offline:** when the internet is down, the gateway keeps its LAN links,
  buffers `events` and `telemetry` (ring buffer of 1000 entries or 1 MB),
  never stores commands, and on reconnect republishes `state` and
  `config/reported` for every child.

## 10. Cloud ↔ app

- **REST:** see [CLOUD_API.md](CLOUD_API.md), which is generated from
  OpenAPI from P2 onwards.
- **WebSocket:** `wss://…/api/v1/events?access_token=…` sends
  `{"v":1,"event":"device.state","ts":…,"data":{…}}`. The events are:
  - `device.online` and `device.offline`,
  - `device.state`,
  - `device.config`,
  - `command.updated`,
  - `ota.progress`,
  - `gateway.children`.

  Events are filtered to the homes the user belongs to.
