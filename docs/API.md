# API Reference

Lumen exposes one REST API and one WebSocket on the controller's web server,
plus optional MQTT, TCP and UDP integrations. Every interface is versioned and
authenticated; none of them touches GPIO directly — they enqueue commands to
the lighting engine (AGENTS.md §4.7, §4.11).

Source of truth: `src/web/api_*.cpp` (firmware) and `webui/src/schemas`,
`webui/src/screens/*/api.ts` (Zod schemas used by the UI).

## 1. Conventions

- Base path `/api/v1`. JSON in, JSON out, UTF-8.
- Every response uses the envelope
  `{"success": true, "data": <payload>, "error": null}` or
  `{"success": false, "data": null, "error": {"code": "...", "message": "..."}}`.
- Error codes (HTTP status): `INVALID_REQUEST`, `INVALID_CONFIGURATION`,
  `INVALID_COMMAND`, `WEAK_PASSWORD`, `CURRENT_PASSWORD_WRONG`,
  `CONFIRMATION_REQUIRED` (400); `UNAUTHORIZED`, `INVALID_CREDENTIALS` (401);
  `PASSWORD_CHANGE_REQUIRED`, `CSRF_FAILED`, `READ_ONLY` (403); `NOT_FOUND`
  (404); `METHOD_NOT_ALLOWED` (405); `CONFLICT`, `CAPACITY`, `IN_USE` (409);
  `PAYLOAD_TOO_LARGE` (413); `UNSUPPORTED_PROPERTY` (422); `RATE_LIMITED`
  (429); `INTERNAL_ERROR`, `STORAGE_ERROR` (500); `BUSY`, `HARDWARE_ERROR` (503).
- Request bodies are bounded (16 KB by default, tighter on routes that need less; 413 when exceeded).
- Responses carry `Cache-Control: no-store`, `X-Content-Type-Options: nosniff`,
  `X-Frame-Options: DENY` and a restrictive `Content-Security-Policy`.

## 2. Authentication

| Item | Behaviour |
|---|---|
| Session | `POST /auth/login {username, password}` sets `lumen_session` (256-bit, `HttpOnly; SameSite=Strict`). Idle timeout 2 h, absolute 7 days, 8 sessions max (oldest evicted). Sessions live in RAM: a reboot signs everyone out. |
| CSRF | Login/session responses return `csrfToken`; every non-GET request must send it in `X-CSRF-Token`. |
| First login | `admin` / `admin` signs in with `forcePasswordChange: true`; only `/auth/*` is reachable until `PUT /auth/password` succeeds (min. 10 characters). |
| Passwords | PBKDF2-HMAC-SHA256, per-user 16-byte salt, 10 000 iterations, stored in NVS. |
| Rate limits | 5 failed logins per IP per 5 min; 10 consecutive failures lock the account for 5 min. |

Route access levels: **public** (`/auth/login`, `/auth/session`, `/system/health`),
**password-change** (reachable with a forced-change session), **user** (everything else).

## 3. Endpoints

### Auth
| Method | Path | Notes |
|---|---|---|
| POST | `/auth/login` | → `{username, forcePasswordChange, csrfToken}` |
| POST | `/auth/logout` | clears the cookie |
| GET | `/auth/session` | current session or 401 |
| PUT/POST | `/auth/password` | `{currentPassword, newPassword}` |

### System
| Method | Path | Notes |
|---|---|---|
| GET | `/system/health` | public liveness probe |
| GET | `/system/info` | firmware version, board, chip, uptime |
| GET | `/system/diagnostics` | heap, tasks, lighting tick timing, storage use |
| POST | `/system/restart` | restarts after 1.5 s; pending light state is saved and MQTT publishes `offline` first |
| POST | `/system/factory-reset` | erases configuration and credentials (confirm in UI) |

### Lights
| Method | Path | Notes |
|---|---|---|
| GET | `/lights`, `/lights/{id}` | config + `state` (requested) + `output` (applied) |
| GET | `/lights/effects` | supported effect names |
| POST | `/lights` | `{id, name, type, roomId?, driver:{type, deviceId, channel}, transitionMs?, …}` |
| PUT / DELETE | `/lights/{id}` | delete also detaches the light from rooms, groups and scenes |
| POST | `/lights/{id}/command` | 202; see below |

Light command body (all fields optional, at least one required):
`power` (bool), `toggle` (bool), `brightness` 0–100, `red`/`green`/`blue`/`white`
0–255, `colorTemperature` 1000–20000 K, `transitionMs` 0–600000,
`effect` (name, `stop`/`none` cancels) with `effectParams {speedMs, durationMs}`,
`origin`, `commandId`. Fields a light does not support are ignored. A newer
command replaces an active transition or effect.

### Rooms, groups, scenes
| Method | Path |
|---|---|
| GET/POST | `/rooms`, `/groups`, `/scenes` |
| GET/PUT/DELETE | `/rooms/{id}`, `/groups/{id}`, `/scenes/{id}` |
| POST | `/rooms/{id}/power`, `/rooms/{id}/brightness` |
| POST | `/groups/{id}/command`, `/groups/{id}/power`, `/groups/{id}/brightness` |
| POST | `/scenes/capture`, `/scenes/{id}/activate`, `/scenes/{id}/duplicate` |

### Hardware and board profiles
| Method | Path | Notes |
|---|---|---|
| GET | `/hardware/status`, `/hardware/inputs`, `/hardware/outputs` | driver health, inputs, outputs |
| GET | `/hardware/outputs/{id}/readback` | PCA9685 register read-back `{percent}` (verification) |
| POST | `/hardware/outputs/{id}/test` | timed test pulse |
| GET | `/hardware/i2c/scan` | addresses found on the bus |
| GET | `/boards`, `/boards/active`, `/boards/{id}`, `/boards/{id}/pins` | built-in + custom profiles |
| POST | `/boards/validate`, `/boards`, `/boards/{id}/duplicate` | validation by `PinValidator` |
| PUT/DELETE | `/boards/{id}` | custom profiles only |
| POST | `/boards/apply`, `/boards/{id}/apply` | transactional: rolls back if drivers fail |

### Automations
| Method | Path |
|---|---|
| GET/POST | `/automations` |
| GET/PUT/DELETE | `/automations/{id}` |
| POST | `/automations/{id}/enable`, `/disable`, `/test`, `/cancel` |
| GET | `/automations/history`, `/automations/{id}/history` |
| GET/PUT | `/automations/settings` (`{latitude, longitude}` for sun triggers) |
| GET, PUT | `/automations/variables`, `/automations/variables/{name}` |

Document format: see `docs/AUTOMATION.md`.

### Network
| Method | Path | Notes |
|---|---|---|
| GET | `/network`, `/network/status`, `/network/scan` | settings (no secrets), live status, Wi-Fi scan |
| GET | `/network/ap-password` | hotspot password, on explicit request |
| PUT | `/network/wifi`, `/network/ap`, `/network/ethernet`, `/network/system` | Wi-Fi password write-only |

### Integrations
| Method | Path | Notes |
|---|---|---|
| GET | `/integrations` | `{mqtt, control, baseTopic}` with live status counters |
| PUT | `/integrations/mqtt` | `{enabled, host, port, tls, username, password?, clientId, topicPrefix, controllerId, qos 0–1, keepalive 10–3600, retain}`; password is write-only (NVS), omit to keep |
| POST | `/integrations/mqtt/test` | `{connected, message}`; publishes a test event when connected |
| PUT | `/integrations/homeassistant` | `{enabled, prefix}` |
| PUT | `/integrations/tcp`, `/integrations/udp` | `{tcpEnabled, tcpPort, udpEnabled, udpControl, udpAnnounce, udpPort}` |
| GET | `/integrations/token` | integration token (signed-in user only) |
| POST | `/integrations/token/regenerate` | invalidates every TCP/UDP client |

## 4. WebSocket — `/api/v1/ws`

The upgrade request must carry a valid session cookie (refused with 401
otherwise). Server → client messages are `{"type": "...", "data": {...}}`:

| Type | Data |
|---|---|
| `light_state_changed` | light id, requested and applied state (coalesced per light) |
| `group_state_changed` | `{groupId}` |
| `scene_activated` | `{sceneId}` |
| `input_state_changed` | input id, name, state |
| `config_changed` | `{kind}` — refetch that collection |
| `hardware_status_changed` | `{reason}` |
| `automation_executed` | history entry |
| `network_status`, `system_status` | periodic status |

Clients may send `{"type":"ping"}`; other client messages are ignored.

## 5. MQTT and Home Assistant

Base topic `{topicPrefix}/{controllerId}` (defaults `lighting/<hostname>`).

| Topic | Direction | Payload |
|---|---|---|
| `base/status` | out, retained, LWT | `online` / `offline` |
| `base/light/{id}/set` | in | HA JSON schema: `state` ON/OFF/TOGGLE, `brightness` 0–100, `color {r,g,b[,w]}`, `white`, `color_temp` (kelvin — discovery sets `color_temp_kelvin: true`; values < 1000 are read as mireds) or `color_temp_kelvin`, `transition` (s, 0–600), `effect` |
| `base/light/{id}/state` | out, retained | same schema |
| `base/group/{id}/set` | in | same schema, fanned out to members |
| `base/scene/{id}/activate` | in | any payload |
| `base/event` | out | inputs, scenes, test events |
| `base/automation/{id}/status` | out, retained | last execution |
| `base/diagnostics` | out, every 60 s | `{uptime, free_heap, rssi, version}` |

Invalid payloads are rejected and counted (`status.rejected`); nothing partial
is applied. State uses the same keys (`color_temp` in kelvin). With discovery
on, `{haPrefix}/light/{controllerId}/{light}/config` (schema `json`,
`brightness_scale` 100, supported colour modes, kelvin range and effects per
light) and three diagnostic sensors are published, retained. The device is
named `Lumen <suffix>` (`lumen-0798` → "Lumen 0798") and entity ids default to
`light.<controller>_<light>` via `default_entity_id` (`object_id` is ignored by
current Home Assistant); users can rename entities in HA afterwards. When a light is
deleted, or discovery / MQTT is turned off, its config and retained state are
cleared with empty retained payloads, so Home Assistant removes the entity.

Publishing is done from the housekeeping task outside the model lock; light
states are coalesced (only the latest state per light is sent). TLS uses the
ESP-IDF certificate bundle (server certificate verified).

## 6. TCP control — port 8266 (off by default)

Newline-delimited JSON, protocol `"v":1`, max 4 clients, 1 KB lines.

```
→ {"v":1,"type":"auth","token":"<integration token>"}     must be first, within 10 s
← {"v":1,"type":"auth","ok":true}                          (ok:false closes the connection)
→ {"v":1,"id":1,"type":"light","light_id":"ceiling","state":"ON","brightness":60}
→ {"v":1,"id":2,"type":"group","group_id":"downstairs","state":"OFF"}
→ {"v":1,"id":3,"type":"scene","scene_id":"evening"}
→ {"v":1,"id":4,"type":"get"[,"light_id":"ceiling"]}      ← {"v":1,"id":4,"lights":[…],"ok":true}
→ {"v":1,"id":5,"type":"ping"}                             ← {"v":1,"id":5,"type":"pong","ok":true}
← {"v":1,"id":N,"ok":false,"error":"brightness must be 0-100"}
← {"v":1,"type":"state","light_id":"ceiling", …}           pushed on every change
```

Light and group commands take the MQTT/HA field set. Idle connections close
after 300 s; send `ping` as a heartbeat. TCP keepalive (30 s idle, 3 × 5 s
probes) frees the slot of a peer that disappears without closing.

## 7. UDP — port 8267

- **Discovery** (on by default): send `LUMEN?` or `{"v":1,"type":"discover"}`;
  the controller answers unicast
  `{"v":1,"type":"announce","id","ip","http","tcp"?,"udpControl","version"}`.
- **Control** (off by default): signed frames
  `{"v":1,"ts":<unix s>,"seq":<u32>,"src":"<sender ≤32>","cmd":{…TCP command…},"mac":"<hex>"}`,
  `mac = HMAC-SHA256(token, "1|ts|seq|src|" + compact(cmd))` where `compact`
  is the command JSON without whitespace, keys in sent order, integer numbers.
  Accepted only with `|ts − now| ≤ 30 s` (the controller clock must be
  NTP-synchronised), a per-sender increasing `seq`, and at most 20 frames/s.
  Valid frames get `{"v":1,"seq":N,"ok":…}`; invalid ones are dropped silently
  and counted. Reference client: `test/device/lumen_udp.py`.
- **Announce** (off by default): state changes are broadcast as TCP `state` lines.

The integration token is generated on first boot (NVS only; never in JSON,
exports or logs) and can be regenerated from the UI.
