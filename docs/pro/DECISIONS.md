# Lumen Pro — Decision Record

Each entry is a decision that shapes the Lumen Pro system: what was decided,
why, and what it rules out. The product brief is [VISION.md](VISION.md). Where
a decision changes the brief, the entry says so. Entries are never deleted;
a reversed decision gets a new entry that supersedes it.

Status: **Accepted** (owner-approved or required by an accepted one),
**Proposed** (planner's choice, open to owner review), **Open** (needs an
owner decision before the phase that depends on it).

---

## D-01 Commercial, proprietary product — Accepted (owner, 2026-11-27)

Lumen Pro is closed source. Work happens on the `lumen-pro` branch of the
private repository, under the proprietary [LICENSE](../../LICENSE).

- The MIT-licensed 0.1.0 code that was public stays MIT for anyone who
  obtained it. Lumen Pro must not depend on keeping that code secret.
- Third-party licences are honoured. `THIRD_PARTY_NOTICES.md` is generated
  at release time for every product (firmware, web UI, cloud, app, gateway).
- New dependencies must have licences that allow closed-source commercial
  use (no GPL/AGPL in shipped binaries without a review, and no "source
  available" licences that forbid hosting a competing service unless the
  terms are checked; see D-12).

## D-02 Monorepo — Accepted (owner)

```text
Lumen/                 (branch lumen-pro)
  src/ include/ webui/ boards/ schemas/ test/   controller firmware + local web UI (unchanged layout)
  protocol/            versioned cloud/gateway/local-ticket contracts: JSON Schemas + fixtures
  cloud/               Lumen Cloud API (Python, FastAPI) + infrastructure
  gateway/             Lumen Gateway (Go) for Linux mini PCs (Raspberry Pi etc.)
  mobile/              Lumen mobile app (React Native, Expo, TypeScript)
  docs/pro/            Lumen Pro system documentation
  .opencode/plans/pro/ Lumen Pro phase plan
```

A protocol change is one commit touching `protocol/` and every consumer.
Changes the brief (§4 said separate repositories).

## D-03 Gateway runs on Linux, written in Go — Accepted (owner)

The gateway is a single static Go binary (`linux/arm64`, `linux/armv7`,
`linux/amd64`), shipped as a `.deb` with a systemd unit. Go gives a small
memory footprint, simple atomic self-updates (replace one file) and easy
cross-compilation. Protocol types are written in Go by hand. Contract tests
against `protocol/fixtures` keep them identical to the cloud (Pydantic) and
app (Zod) models.

## D-04 Gateway talks to controllers over the LAN — Accepted (owner)

The gateway reaches controllers through their **authenticated local API**
over the home's Wi-Fi/Ethernet:

- REST commands,
- WebSocket live state,
- UDP discovery.

No extra radio hardware is needed. An ESP-NOW USB bridge stays possible
later as a second transport behind the same `ControllerLink` interface in
the gateway. It is not in v1. Changes the brief (§10.3 assumed ESP-NOW
bridging).

## D-05 Cloud mode is selectable per controller: off / direct / gateway — **Superseded by D-17** (owner, 2026-11-27)

Each controller has one setting, `cloud.mode`:

| Mode | The controller... |
|---|---|
| `off` (default) | has no cloud traffic at all. Fully local, as in 0.1.0. |
| `direct` | holds its own MQTT-over-TLS session to the Lumen Cloud broker. |
| `gateway` | never connects to the cloud. Its assigned gateway represents it. |

The mode is set in the controller's local web UI or by the mobile app
during provisioning. Exactly one cloud transport owns a controller at a
time. Changing the mode re-assigns ownership in the cloud: the old route is
revoked before the new one is used. Automatic direct ⇄ gateway failover is
not in v1 (as in the brief).

## D-06 Direct mode must prove it fits in memory before it ships — **Obsolete** (D-17 removes direct mode)

- **Measured on the KC868-AP:** in normal operation the free heap is flat at
  about 61 KB. Under heavy load the lowest seen was about 22 KB. The
  firmware already fills 91 % of its 1.5 MB slot.
- **The cost of direct mode:** one TLS session typically needs 35–45 KB of
  heap. A user who also runs the TLS Home Assistant MQTT integration would
  need two.
- **What goes first:** Phase P5 starts with a measured spike: a cloud MQTT
  TLS session plus normal load on the real board.
- **Gate:** under load, the minimum free heap stays at 20 KB or more with
  direct mode on.
- **Allowed fixes:**
  - mbedTLS asymmetric buffers (`MBEDTLS_SSL_IN_CONTENT_LEN` /
    `OUT_CONTENT_LEN`) and dynamic buffers,
  - refusing direct mode while the external MQTT integration uses TLS.

  The device UI must say why.
- **If the gate cannot be met:** direct mode is limited to board profiles
  with PSRAM or 8 MB flash, and the KC868-AP supports `gateway` only. The
  firmware reads this from a board capability, never from a hardcoded board
  check.

## D-07 Controllers stay the authority for their configuration — Proposed (changes brief §13.1)

The brief makes the cloud the authority for light configuration, scenes and
automations. But the local web UI also edits them, and it does so offline.
Two writers with no merge rule cause lost updates.

**Decision:** the controller is the single authority.

- The cloud keeps a **mirror** from `config/reported` (with a revision
  number).
- Edits from the app (rename a light, create a scene, edit an automation)
  are sent to the controller as validated **config commands**.
- The controller applies them through its existing transactional
  `ConfigManager` and reports the new revision.
- An edit made against an old revision is refused with `CONFLICT`, and the
  app refetches.
- Cloud-only data (homes, members, room assignment across controllers,
  display order, icons) lives in the cloud and is never sent to the device.

Hardware profiles and pin maps are never editable from the cloud (as in
the brief).

## D-08 MQTT topics are keyed by device, not by home — Proposed (changes brief §8.2)

`lumen/v1/d/{device_id}/…` and `lumen/v1/g/{gateway_id}/…`.

If `home_id` were in the topic, moving a device to another home or owner
would mean new topics, new ACLs and new firmware subscriptions. Instead,
the home mapping lives only in the cloud database. A gateway publishes and
subscribes on `lumen/v1/d/{child}/…` for the children assigned to it, and
the broker authorisation hook checks that assignment on every request.
Details: [PROTOCOL.md](PROTOCOL.md).

## D-09 No retained desired light state — Proposed (changes brief §8.2)

Remote light control uses **commands only**. Each command has a command ID
and an expiry, and is never retained. A retained `state/desired` topic
would replay an old wish when a device reconnects, which the brief forbids
(§9.7).

Desired/reported revisions are used for **configuration** only, through
config commands and the reported mirror (D-07).

## D-10 Identity and claiming work for boards Lumen did not manufacture — Proposed (extends brief §7)

Today Lumen runs on third-party boards (KC868-AP, generic ESP32). There is
no Lumen factory yet. So v1 claims a device with a **short-lived,
single-use claim token** that the cloud issues to a signed-in user.

1. The app obtains the claim token.
2. It hands the token to the controller over the local network or the
   setup hotspot.
3. The controller passes it to its gateway, and the gateway redeems it
   with the cloud (D-17).

The cloud then assigns:

- the **device ID** (UUIDv7),
- a **local key** (`K_local`, 32 random bytes; stored encrypted).

The MAC address is kept as metadata only. Factory provisioning is a later
phase (P11): pre-registered devices with a QR claim code and per-device
certificates, for Lumen-branded hardware.

## D-11 Local access uses cloud-signed tickets, never the admin password — Proposed (answers brief §15.3)

The controller's admin password is never sent to the cloud. Phones and the
gateway reach a controller's local API with a **local ticket** instead.

- **What a ticket is:** a compact token signed with HMAC-SHA256 using
  `K_local`.
- **What it says:**
  - user (or gateway) ID,
  - device ID,
  - scope (`control` or `admin`),
  - issued and expiry times,
  - a revocation epoch.
- **Who issues it:** the cloud, to members the home's roles allow. In
  gateway mode the gateway also issues them, for its own calls.
- **How the controller checks it:** offline, with no cloud round trip.
  - A controller that rebooted without internet compares the expiry with a
    persisted "last known time".
  - Raising the epoch, which the cloud does through a config command,
    revokes every older ticket.
- **How it is sent:** `Authorization: Lumen-Ticket <ticket>`. Tickets are
  not ambient credentials, so CSRF does not apply to them.
- **What it can reach:**
  - Scope `control`: lights, groups, rooms, scenes, read-only status, and
    the WebSocket.
  - Scope `admin`: everything the signed-in local user can reach, except
    password and factory reset.

This is a change to the firmware's security defaults (AGENTS.md §15). The
owner approved it by commissioning this system. The local session login is
unchanged.

**Known limitation (v1):** the controller serves plain HTTP. A ticket
captured on the LAN can be replayed until it expires, just like today's
session cookie. Mitigations:

- short expiry (12 h for `admin`, 7 days for `control`),
- the epoch revocation above,
- local TLS later (P11).

## D-12 Broker: EMQX, behind an HTTP auth/ACL hook — Accepted with an open licence check

- **Integration:** EMQX authenticates and authorises clients by calling the
  cloud's internal `/internal/mqtt/auth` and `/internal/mqtt/acl`
  endpoints. The cloud database stays the single source of truth for
  credentials and gateway–child assignments, and revocation takes effect
  immediately (with a short ACL cache).
- **Pinned version:** development uses EMQX 5.8.x.
- **Open:** EMQX's licensing changed in 2025 (Business Source License for
  newer releases). The production licence must be checked before launch.
- **Fallback:** Mosquitto with an HTTP auth plugin. It uses the same hook
  contract, so the cloud code does not change.

## D-13 Cloud stack and infrastructure — Accepted (brief §5.1)

- **Application:**
  - Python 3.12 and FastAPI,
  - SQLAlchemy 2 (async) with asyncpg, Alembic migrations and PostgreSQL 16,
  - Pydantic v2,
  - an aiomqtt backend client,
  - argon2id password hashing,
  - JWT access tokens (15 min) plus rotating, family-tracked refresh tokens
    (30 days; reuse revokes the family).
- **Background work and messaging:** Redis only where a worker or
  cross-instance pub/sub needs it (WebSocket fan-out from P4).
- **Storage:** S3-compatible object storage for firmware.
- **Packaging and monitoring:**
  - Docker Compose for development,
  - a single "modular monolith" container image,
  - Prometheus metrics.

## D-14 Mobile app stack — Accepted (brief §12.1)

- **Core:** React Native with Expo (SDK pinned at P7 start), TypeScript
  strict, Expo Router.
- **Data, state and forms:** TanStack Query, Zustand, Zod, React Hook Form.
- **Device features:**
  - expo-secure-store for refresh tokens and tickets,
  - expo-camera for QR scanning.
- **Provisioning:** v1 uses the controller's existing WPA2 setup hotspot,
  plus LAN discovery (mDNS/UDP). BLE is not used. Bluetooth is disabled in
  the firmware, and there is no flash room for the BLE stack on a 4 MB
  board (D-06).
- **Gateway provisioning:** v1 uses LAN discovery plus a setup code printed
  by the gateway.

## D-15 Configuration and secret storage on the controller — Accepted (brief §9.3)

- **Settings:** `config/cloud.json` holds mode, gateway ID and URL,
  device ID and ticket epoch. It holds no secrets.
- **Secrets:** a separate NVS namespace `cloud` holds `K_local` and the
  last known time. Since D-17 there is no MQTT secret on a controller.
- **Separation from the existing integration:** nothing is shared with
  `integrations/mqtt.json` or the `mqtt` NVS namespace. `MqttService` is
  not modified.
- **Factory reset** erases both.
- **Backups** never contain the secrets.

## D-16 Phase order — Accepted (brief §17, adjusted)

The order is protocol first, then cloud, firmware, gateway, app, sync and
OTA. The mobile app starts only when the cloud API and identity model are
stable and a scripted client has passed the end-to-end path through a gateway. See [.opencode/plans/pro/README.md](../../.opencode/plans/pro/README.md).

## D-17 Controllers reach the cloud only through a Lumen Gateway — Accepted (owner, 2026-11-27; supersedes D-05, D-06)

A Lumen controller never connects to Lumen Cloud itself. Every home that
uses the cloud has a Lumen Gateway: Linux on a mini PC (D-03), linked to its
controllers over the LAN (D-04).

**Controller setting:** `cloud.mode` is `off` (default) or `gateway`.

**What this changes:**

- **Firmware:**
  - No cloud MQTT client, no cloud TLS session and no HTTPS registration.
    It gains local tickets (D-11), pairing with a gateway, and a Cloud card
    in the local UI.
  - The brief's `CloudMqttService`, `CloudCommandRouter`,
    `CloudStatePublisher`, `CloudConnectionManager` and `CloudOfflineQueue`
    become gateway modules (Go).
  - The memory risk of D-06 is gone. The controller's flash and heap use
    grow only by the ticket verifier and the pairing endpoint.
- **Cloud:**
  - Only gateways and service clients connect to the broker.
  - There is no controller MQTT credential and no `/device/register`.
  - Controllers are registered by their gateway: PROTOCOL §8.
- **Topics:** unchanged (D-08). The gateway publishes on `d/{child}/…` for
  its children, so the cloud and the app do not care how a controller is
  attached.
- **Commercial:** the gateway is the product that unlocks remote access.
  Controllers stay fully usable locally without it (brief §19 rule: the
  cloud is never needed for local lighting).

## D-18 The gateway is the home's hub — Proposed (owner asked for "extra features")

The gateway does more than relay to the cloud. The first-release list
(P6) is the cloud bridge plus fleet management:

1. **Fleet management:**
   - discovers every Lumen controller on the LAN and adopts it (pairing);
   - shows its health (firmware, uptime, heap, Wi-Fi signal, peripheral
     errors) from `/system/diagnostics`;
   - restarts it, and renames it.
2. **Config backups:** a nightly `GET /backup` of each controller, stored
   on the gateway (last 14 days per controller) and restorable from the
   app. These backups contain no secrets (docs/API.md).
3. **Firmware updates:** the gateway downloads signed releases from the
   cloud, verifies them and installs them on its controllers through
   `/ota/upload` (P9). It does staged rollouts within a home: one
   controller first, the rest after it reports healthy.
4. **Home-wide scenes and automations (P8+):**
   - scenes that span several controllers, run on the gateway, sending
     each controller its part over the LAN;
   - automations whose trigger and actions are on different controllers.

   Each controller's own automations still run on the controller (brief
   rule).
5. **Time and presence of the LAN:**
   - an NTP source for controllers without internet access;
   - LAN reachability of the phone (route selection).

Candidate features, each needing an owner decision before it is planned:

- energy and usage history (on-time, brightness × time per light);
- a local web dashboard on the gateway;
- HomeKit / Matter bridge (the gateway, not the 4 MB controller, would
  host it);
- voice assistants through the cloud;
- an ESP-NOW USB radio for controllers without Wi-Fi (D-04);
- a Zigbee coordinator for third-party bulbs.
