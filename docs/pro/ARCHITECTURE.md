# Lumen Pro — System Architecture

Read [VISION.md](VISION.md) (product brief), then [DECISIONS.md](DECISIONS.md)
and [PROTOCOL.md](PROTOCOL.md). The controller firmware's own architecture is
in [../ARCHITECTURE.md](../ARCHITECTURE.md).

**Ownership rule (brief):**

- The firmware owns the physical lighting.
- The cloud owns remote identity, access and message routing.
- The app owns the customer experience.
- The gateway is the home's hub and its only cloud connection (D-17, D-18).

## 1. Components

```text
                 Phone: Lumen app (Expo)
           remote |                         | local (same LAN)
     HTTPS + WSS  |                         | HTTP + WS, Lumen-Ticket
                  v                         v
        +------------------+     +-----------------------------+
        |   Lumen Cloud    |     |  Controller local API       |
        | FastAPI monolith |     |  (existing firmware REST/WS)|
        |  PostgreSQL      |     +-----------------------------+
        |  Redis (fan-out) |                ^
        +--------+---------+                | LAN: REST + WS (g: ticket),
          aiomqtt|  ^ auth/ACL hook         |      UDP/mDNS discovery
                 v  |                       |
        +------------------+    MQTT/TLS   +----------------------------+
        |  EMQX broker     |<------------->|  Lumen Gateway (Go, Linux) |
        +------------------+  (only        |  cloud bridge, fleet mgmt, |
                              gateways)    |  backups, OTA relay, hub   |
                                           +----------------------------+
                                                        |
                                     Controllers (cloud.mode = gateway)
```

Controllers never talk to the cloud (DECISIONS D-17). The gateway is the
home's only cloud connection.

## 2. Data ownership

| Data | Owner | Copies |
|---|---|---|
| Light output (actual state) | controller | gateway (live), cloud `device_state` (from `state`), app cache; marked stale when offline |
| Lights, rooms, groups, scenes, automations | controller (D-07) | gateway and cloud mirrors (`config/reported`, with revision) |
| Hardware profile, pins | controller | never leaves the device, except the read-only `board` ID |
| Users, homes, members, roles | cloud | app cache |
| Device ↔ home, gateway ↔ child assignment | cloud | gateway (`g/{gw}/children`) |
| Gateway MQTT secret | cloud (SHA-256 hash) + gateway secrets file (0600) | — |
| `K_local` (per controller) | cloud (AES-GCM encrypted) + controller NVS + its gateway | — |
| Controller config backups | gateway (D-18, 14 days) | — |
| Local admin password | controller only | never |

## 3. Main flows

**Remote control:**

1. App → `POST /devices/{id}/commands`.
2. Cloud checks the user's role for the home, stores the command as
   `queued`, publishes it to `d/{id}/cmd` with an MQTT 5 expiry, and marks
   it `sent`.
3. The gateway, which subscribes to `d/{child}/cmd`, checks it (PROTOCOL
   §3) and calls the controller's local REST API with a `g:` ticket.
4. The controller validates the request and enqueues it on its lighting
   queue, exactly like a local REST call.
5. The gateway publishes `applied` once the controller's WebSocket confirms
   the new output (or `rejected`, mapped from the HTTP error). It then
   publishes the coalesced `state`.
6. The cloud updates the command and the state, and fans out over the
   WebSocket. The app clears the pending state when the reported output
   arrives.

**Local control:**

1. The app keeps a `control` ticket per controller, from
   `POST /devices/{id}/local-ticket`. The ticket is refreshed while the app
   is online and stored in secure storage.
2. On the home LAN, the app finds the controller (mDNS or UDP discovery,
   last known IP) and talks to it directly. If the gateway is down, local
   control still works.
3. The app shows a route badge: Local, Remote or Unavailable.

## 4. Failure behaviour

| Failure | Behaviour |
|---|---|
| Internet, cloud or broker down | No change for: lighting, inputs, scenes, automations, the local UI, local app control (tickets still valid), HA MQTT and the gateway's LAN features. Remote control is unavailable, and the app shows "Unavailable" when away. Commands are never queued for later. |
| Gateway down | Controllers keep working locally, including local app control. The cloud marks the children `offline` (gateway LWT). |
| Controller reboot or IP change | The gateway re-discovers it by `controller_id` and republishes its retained `state` and `config/reported`. |
| Gateway reconnect | Backoff from 1 s to 5 min with ±20 % jitter, then a republish of every child. |
| Cloud DB restore or rollback | A gateway whose credential is unknown gets `authentication_failed`. It shows "re-claim needed" in the app and keeps its LAN features. Controllers are never touched. |

## 5. Deployment (cloud)

- **Development:** `cloud/docker-compose.yml` runs `api`, `postgres:16`,
  `emqx:5.8`, `redis:7`, and optionally `prometheus`.
- **Production (P11):**
  - one container image for the API, run as N stateless replicas,
  - managed PostgreSQL with point-in-time recovery,
  - EMQX on 1–3 nodes behind TLS (port 8883),
  - S3 storage for firmware.

  Secrets come from the platform's secret store, never from the image.

## 6. Repository map

| Path | Product | Build / test |
|---|---|---|
| `src/`, `include/`, `webui/` | controller firmware + local UI | `pio run -e esp32`, `pio test -e native`, `cd webui && npm test` |
| `protocol/` | contract schemas + fixtures | `npm run validate:schemas` (root) |
| `cloud/` | Lumen Cloud | `cd cloud && make test` (pytest, Postgres via compose) |
| `gateway/` | Lumen Gateway | `cd gateway && go test ./...` |
| `mobile/` | Lumen app | `cd mobile && npm test` |
