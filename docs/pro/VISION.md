> **Product brief (owner, 2026-11-27), kept as written.** Where it conflicts
> with [DECISIONS.md](DECISIONS.md), the decisions win. The most important
> one: **controllers reach Lumen Cloud only through a Lumen Gateway (D-17)**,
> so the brief's direct controller-to-cloud path and firmware `CloudMqttService`
> become gateway modules. See also D-02 (monorepo), D-03 (Go gateway),
> D-07 (configuration authority), D-08/D-09 (topics), D-14 (no BLE in v1).

# Lumen Cloud & Mobile App — Development Guide

**Project:** Lumen  
**Repository:** https://github.com/charithmadhuranga/Lumen  
**Document type:** Engineering development guide  
**Target:** Production-ready cloud-connected Lumen platform  
**Architecture:** Local-first, hybrid connectivity, managed cloud  
**Firmware:** ESP-IDF 6.1, PlatformIO, C++17, FreeRTOS  
**Cloud backend:** Python, FastAPI, EMQX, PostgreSQL  
**Mobile:** React Native, Expo, TypeScript  

---

# 1. Project Overview

## 1.1 Objective

Extend the existing Lumen ESP32 smart lighting firmware with a managed cloud platform and a mobile application.

The system must allow customers to:

- Control Lumen lighting controllers locally.
- Control lighting controllers remotely over the internet.
- Manage multiple controllers from one mobile application.
- Manage homes, rooms, groups and scenes.
- Configure and synchronize supported automations.
- Monitor device connectivity and health.
- Provision new controllers.
- Receive firmware updates.
- Continue using lighting and automations when the cloud or internet is unavailable.

The implementation must preserve the existing Lumen firmware architecture and functionality.

**Important architectural decision: Lumen Cloud is an API-only backend.**

There will be no Lumen Cloud web dashboard, cloud administration website or customer-facing cloud frontend.

The mobile application is the customer's primary interface for managing and controlling Lumen devices through the cloud.

The existing Lumen firmware-hosted web application remains available for local access and advanced controller configuration.

## 1.2 Product components

The complete system consists of three products.

### Lumen Firmware

Runs on ESP32 and ESP32-S3 controllers.

Responsibilities:

- Lighting hardware control.
- Switch and dimmer management.
- RGB, RGBW and CCT lighting.
- Rooms, groups and scenes.
- Local automation execution.
- Local REST API and WebSocket.
- Existing Home Assistant MQTT integration.
- ESP-NOW multi-controller communication.
- Cloud MQTT communication.
- Local configuration and persistence.
- OTA firmware management.

### Lumen Cloud

A headless backend service.

Responsibilities:

- Customer authentication.
- Home and membership management.
- Device registration and ownership.
- Device authentication and MQTT authorization.
- Remote command routing.
- Device state synchronization.
- Gateway registration and management.
- Configuration synchronization.
- OTA release and update orchestration.
- Event history and notifications.
- Mobile application API.

Lumen Cloud exposes APIs and messaging interfaces only. It does not render customer-facing pages.

### Lumen Mobile App

The customer-facing application for Android and iOS.

Responsibilities:

- User registration and login.
- Home and room management.
- Device onboarding.
- Local and remote lighting control.
- Scene management.
- Automation management.
- Gateway management.
- Device monitoring.
- OTA management.
- Notifications.

---

# 2. Mandatory Existing Firmware Compatibility

The existing firmware must remain the foundation of this project.

Do not rewrite the lighting engine, hardware abstraction layer, automation engine or existing networking architecture to accommodate cloud support.

Before implementation, inspect:

- `AGENTS.md`
- `SPEC.md`
- `SKILL.md`
- `docs/ARCHITECTURE.md`
- `docs/API.md`
- `docs/IMPLEMENTATION_STATUS.md`
- `docs/HARDWARE_PROFILES.md`
- `docs/ESP_NOW_PROTOCOL.md`
- `src/networking/mqtt_service.cpp`
- Existing lighting and command-routing modules.

Follow the repository's existing development phases, testing conventions and coding-agent rules.

## 2.1 Existing architecture

The current firmware has:

- ESP-IDF 6.1 and PlatformIO.
- C++17.
- FreeRTOS.
- Hardware abstraction layer.
- Versioned JSON board profiles.
- Lighting engine with a bounded command queue.
- Event bus.
- Local automation engine.
- REST API and WebSocket.
- MQTT and Home Assistant discovery.
- TCP and UDP control.
- ESP-NOW multi-controller synchronization.
- NVS and LittleFS persistence.
- Dual-slot OTA.
- Existing local React web application.

The lighting engine is independent of networking.

All network interfaces enqueue commands rather than directly accessing hardware.

This architectural rule must remain mandatory.

## 2.2 Existing MQTT integration must be preserved

The current firmware already implements `MqttService`.

This service handles the configurable MQTT integration, including:

- External MQTT broker configuration.
- MQTT state and command communication.
- Home Assistant MQTT discovery.
- Broker credentials.
- Existing MQTT topics.
- Integration status and diagnostics.

**Do not repurpose this service as the Lumen Cloud connection.**

Lumen Cloud and the existing customer-configured MQTT integration are separate connections with separate credentials, settings and responsibilities.

Introduce a dedicated `CloudMqttService`.

The two services must be able to operate simultaneously without interfering with each other.

| Capability | Existing MqttService | New CloudMqttService |
|---|---|---|
| Purpose | External MQTT / Home Assistant | Managed Lumen Cloud |
| Broker | Customer-configured | Lumen-managed |
| Credentials | Customer configuration | Device-specific identity |
| Topics | Existing topic prefix | Lumen Cloud namespace |
| State | Existing integration state | Cloud-reported state |
| Commands | Existing MQTT commands | Authorized remote commands |
| Availability | Existing MQTT status | Cloud device availability |

Do not rename or remove existing MQTT API endpoints.

## 2.3 Preserve the lighting command path

Cloud commands must enter the same internal command-routing system used by existing local REST, MQTT and ESP-NOW commands.

The desired flow is:

```text
Cloud MQTT
    |
    v
CloudMqttService
    |
    v
CloudCommandRouter
    |
    v
Validate command
    |
    v
Existing bounded lighting command queue
    |
    v
Existing lighting engine
    |
    v
HAL and hardware drivers
```

Do not implement separate relay, PWM or light-control logic for remote commands.

---

# 3. Target System Architecture

## 3.1 High-level architecture

```text
                  CUSTOMER
                     |
                     v
             LUMEN MOBILE APP
             React Native / Expo
                     |
           HTTPS REST + WebSocket
                     |
                     v
              LUMEN CLOUD API
                 FastAPI
                     |
          +----------+-----------+
          |          |           |
          v          v           v
       USER       DEVICE      COMMAND
      SERVICE    REGISTRY     SERVICE
          |          |           |
          +----------+-----------+
                     |
                     v
                EMQX BROKER
                     |
          +----------+-----------+
          |                      |
          v                      v
    DIRECT DEVICE            LUMEN HUB
       MQTT                     MQTT
                                |
                                v
                         ESP-NOW NETWORK
                                |
                                v
                         CHILD CONTROLLERS
```

## 3.2 Communication paths

### Direct-connected controllers

Each controller connects to the managed Lumen MQTT broker using its own device credentials.

```text
ESP32 Controller
       |
       | MQTT over TLS
       |
       v
Lumen Cloud MQTT Broker
```

### Gateway-connected controllers

Child controllers communicate with a Lumen Hub using the existing ESP-NOW architecture or an authenticated local protocol.

The gateway connects to Lumen Cloud.

```text
Child ESP32 Controllers
       |
       | ESP-NOW / Local Protocol
       |
       v
Lumen Gateway
       |
       | MQTT over TLS
       |
       v
Lumen Cloud MQTT Broker
```

The gateway must only represent child controllers explicitly assigned to it by the cloud.

Each child retains its own logical device identity.

A controller must have one active cloud transport owner at a time.

For the first implementation, do not implement automatic direct-to-gateway failover. Add that after the initial routing model has been validated.

---

# 4. Repository and Project Organization

Keep the existing Lumen firmware repository intact.

Recommended organization:

```text
Lumen/
│
├── src/
│   ├── existing firmware modules
│   │
│   ├── networking/
│   │   ├── mqtt_service.cpp
│   │   ├── mqtt_service.hpp
│   │   ├── cloud_mqtt_service.cpp
│   │   ├── cloud_mqtt_service.hpp
│   │   ├── cloud_connection_manager.cpp
│   │   ├── cloud_connection_manager.hpp
│   │   ├── cloud_command_router.cpp
│   │   ├── cloud_command_router.hpp
│   │   ├── cloud_state_publisher.cpp
│   │   └── cloud_state_publisher.hpp
│   │
│   ├── services/
│   │   ├── device_identity.*
│   │   ├── cloud_config_sync.*
│   │   └── gateway_manager.*
│   │
│   └── ...
│
├── include/
│   └── lumen/
│       ├── networking/
│       ├── services/
│       └── ...
│
├── boards/
├── schemas/
├── test/
├── docs/
│   ├── CLOUD_ARCHITECTURE.md
│   ├── CLOUD_MQTT_PROTOCOL.md
│   ├── CLOUD_PROVISIONING.md
│   ├── CLOUD_SECURITY.md
│   ├── CLOUD_GATEWAY.md
│   ├── CLOUD_OTA.md
│   └── CLOUD_DEVELOPMENT.md
│
└── ...
```

Create separate repositories for the other products:

```text
lumen-cloud/
│
├── app/
│   ├── api/
│   ├── core/
│   ├── services/
│   ├── mqtt/
│   ├── models/
│   ├── schemas/
│   └── workers/
│
├── migrations/
├── tests/
├── infrastructure/
├── scripts/
├── Dockerfile
├── docker-compose.yml
└── pyproject.toml
```

```text
lumen-mobile/
│
├── app/
│   ├── (auth)/
│   ├── (tabs)/
│   ├── homes/
│   ├── devices/
│   ├── rooms/
│   ├── scenes/
│   ├── automations/
│   ├── gateways/
│   └── settings/
│
├── src/
│   ├── api/
│   ├── auth/
│   ├── components/
│   ├── hooks/
│   ├── mqtt/
│   ├── local/
│   ├── state/
│   ├── schemas/
│   └── types/
│
└── ...
```

The mobile app must never be embedded in the ESP32 firmware filesystem.

---

# 5. Development Phase 1 — Cloud Infrastructure

## 5.1 Technology stack

| Component | Technology |
|---|---|
| Backend | Python + FastAPI |
| MQTT broker | EMQX |
| Database | PostgreSQL |
| Database access | SQLAlchemy |
| Migrations | Alembic |
| Validation | Pydantic |
| Authentication | JWT access tokens + rotating refresh tokens |
| API documentation | OpenAPI |
| Background processing | Redis-backed worker where needed |
| Firmware storage | S3-compatible object storage |
| Containerization | Docker |
| Monitoring | Prometheus + Grafana |

Use a modular monolith for the first release. Do not create unnecessary microservices.

## 5.2 Services

Implement these logical modules:

| Module | Responsibility |
|---|---|
| Authentication | User login, registration, token refresh |
| Home management | Homes, memberships and roles |
| Device registry | Device identity, ownership and capabilities |
| Device provisioning | Device claiming and activation |
| MQTT management | Broker integration and topic permissions |
| Command service | Remote command creation and tracking |
| State service | Device reported state and availability |
| Gateway service | Gateway and child assignments |
| Configuration service | Desired/reported configuration |
| OTA service | Firmware releases and jobs |
| Event service | Event history and notifications |
| Notification service | Mobile push notifications |

## 5.3 Docker development environment

Create a local Docker Compose environment with:

- FastAPI.
- EMQX.
- PostgreSQL.
- Redis if required.
- Optional Prometheus and Grafana.

Provide development-only MQTT credentials and test certificates.

Never commit production secrets.

---

# 6. Development Phase 2 — Database

Use PostgreSQL as the source of truth for cloud identities, ownership, device registration and configuration metadata.

## 6.1 Initial database tables

### Users

```text
users
-----
id
email
password_hash
status
created_at
updated_at
```

### Homes

```text
homes
-----
id
name
timezone
owner_id
created_at
updated_at
```

### Home memberships

```text
home_members
------------
id
home_id
user_id
role
created_at
```

Roles:

- owner
- administrator
- member
- guest

### Devices

```text
devices
-------
id
serial_number
hardware_model
hardware_revision
firmware_version
status
created_at
updated_at
```

### Device assignments

```text
device_assignments
------------------
id
device_id
home_id
room_id
connection_mode
gateway_id
assigned_at
```

### Device credentials

```text
device_credentials
------------------
id
device_id
credential_reference
credential_type
status
created_at
revoked_at
```

Never store plaintext MQTT passwords in the database.

### Gateways

```text
gateways
--------
id
device_id
home_id
status
last_seen_at
```

### Gateway children

```text
gateway_children
----------------
id
gateway_id
child_device_id
route_epoch
assigned_at
```

### Rooms

```text
rooms
-----
id
home_id
name
created_at
updated_at
```

### Device states

```text
device_states
-------------
device_id
reported_state
desired_state
reported_revision
desired_revision
last_seen_at
updated_at
```

### Commands

```text
commands
--------
id
device_id
user_id
operation
payload
status
created_at
expires_at
completed_at
error_code
```

### Scenes

```text
scenes
------
id
home_id
name
definition
created_at
updated_at
```

### Automations

```text
automations
-----------
id
home_id
name
definition
enabled
revision
created_at
updated_at
```

### OTA releases and jobs

```text
ota_releases
------------
id
version
hardware_model
channel
artifact_url
sha256
signature
created_at
```

```text
ota_jobs
--------
id
device_id
release_id
status
progress
started_at
completed_at
error
```

Add foreign keys, uniqueness constraints, indexes and appropriate tenant isolation checks.

Use UUIDs for cloud entities.

---

# 7. Development Phase 3 — Device Identity and Provisioning

## 7.1 Device identity

Every physical Lumen controller needs a unique, immutable identity.

Example:

```json
{
  "device_id": "LUMEN-00000001",
  "hardware_model": "lumen-relay-4",
  "hardware_revision": "1.0",
  "identity_version": 1
}
```

Do not use the ESP32 MAC address as the only commercial identity.

The MAC address can be stored as hardware metadata, but the cloud device identity must remain stable across supported replacement and provisioning workflows.

## 7.2 Manufacturing provisioning

At manufacturing time:

1. Generate a unique device ID.
2. Generate a unique cryptographic identity or credential.
3. Provision it securely.
4. Register the device with Lumen Cloud.
5. Associate the device with its hardware model.
6. Generate a QR code containing the device ID and a one-time claim code.

The QR code must not contain reusable MQTT credentials.

## 7.3 Mobile onboarding

```text
Customer opens Lumen app
          |
          v
Customer logs in
          |
          v
Creates or selects a home
          |
          v
Scans Lumen QR code
          |
          v
Mobile app connects through BLE
          |
          v
Configures controller Wi-Fi
          |
          v
Controller connects to Lumen Cloud
          |
          v
Cloud verifies claim request
          |
          v
Device assigned to customer home
          |
          v
Mobile app loads rooms and lights
```

For a gateway installation, onboard the gateway first and then claim or register its child controllers.

## 7.4 Device credentials

Each direct controller receives unique cloud credentials.

Use TLS and an authentication mechanism suitable for ESP-IDF.

Prefer per-device certificates or strong unique credentials with revocation and rotation support.

Never embed a shared production MQTT username and password into a firmware binary distributed to every customer.

---

# 8. Development Phase 4 — Cloud MQTT Protocol

## 8.1 MQTT architecture

Use EMQX as the managed MQTT broker.

The cloud backend is responsible for authorizing users and managing device ownership.

The mobile app must not receive permanent device MQTT credentials.

Communication:

```text
Mobile App
    |
    | HTTPS API
    v
Lumen Cloud Backend
    |
    | Authorized MQTT publication
    v
EMQX
    |
    v
Lumen Firmware
```

For real-time mobile state updates:

```text
Lumen Firmware
    |
    | MQTT
    v
EMQX
    |
    v
Cloud State / Event Service
    |
    | Cloud WebSocket
    v
Mobile App
```

## 8.2 Topic namespace

Use a versioned topic structure:

```text
lumen/v1/

  homes/{home_id}/

    devices/{device_id}/

      availability
      state/reported
      state/desired

      commands/request
      commands/result

      telemetry
      events

      config/desired
      config/reported

    gateways/{gateway_id}/

      availability
      status
      children
      telemetry
```

The broker must enforce publish and subscribe permissions separately.

A device can only access its authorized topic namespace.

A gateway can only publish or subscribe on behalf of its assigned children.

## 8.3 MQTT topic behavior

| Topic | Publisher | Subscriber |
|---|---|---|
| availability | Device | Cloud |
| state/reported | Device | Cloud |
| state/desired | Cloud | Device |
| commands/request | Cloud | Device |
| commands/result | Device | Cloud |
| telemetry | Device | Cloud |
| events | Device | Cloud |
| config/desired | Cloud | Device |
| config/reported | Device | Cloud |

Use QoS 1 for commands and important state messages.

Use QoS 0 or 1 for telemetry depending on its importance.

Use retained messages for availability and reported state where appropriate.

Never retain command requests.

Use MQTT 5 message expiry and session expiry when supported.

## 8.4 Command format

Example:

```json
{
  "schema_version": 1,
  "command_id": "019ae8a1-7f31-7b44-9d92-001122334455",
  "issued_at": "2026-11-27T10:00:00Z",
  "expires_at": "2026-11-27T10:00:30Z",
  "source": {
    "type": "mobile",
    "user_id": "user_123"
  },
  "target": {
    "type": "light",
    "id": "living_room_main"
  },
  "operation": "light.set",
  "parameters": {
    "power": true,
    "brightness": 75
  }
}
```

Supported operations should initially include:

```text
light.set
light.toggle
light.effect.set
light.effect.stop

scene.activate

room.power
room.brightness

group.power
group.brightness

device.restart
device.status.request
```

Only expose operations that already have a corresponding safe operation in the firmware.

Do not create cloud-only hardware control operations that bypass the lighting engine.

## 8.5 Command acknowledgement

Example:

```json
{
  "schema_version": 1,
  "command_id": "019ae8a1-7f31-7b44-9d92-001122334455",
  "device_id": "LUMEN-00000001",
  "status": "applied",
  "completed_at": "2026-11-27T10:00:01Z"
}
```

Command states:

```text
queued
sent
received
applied
rejected
expired
failed
```

MQTT delivery acknowledgement is not the same as physical command execution.

The mobile application should display success only after receiving an appropriate device-level result.

Use command IDs for deduplication because QoS 1 may deliver duplicate messages.

---

# 9. Development Phase 5 — Firmware Cloud Integration

## 9.1 New firmware modules

Implement:

```text
CloudConnectionManager
CloudMqttService
CloudCommandRouter
CloudStatePublisher
CloudEventPublisher
CloudConfigSync
DeviceIdentity
CloudOfflineQueue
GatewayManager
GatewayMessageBridge
```

Use the existing ESP-MQTT library and project conventions.

Do not create a new MQTT protocol implementation.

## 9.2 CloudConnectionManager

Responsibilities:

- Read cloud connection settings.
- Start and stop the cloud MQTT connection.
- Monitor connection state.
- Handle reconnection.
- Handle credential failures.
- Coordinate with network status.
- Expose cloud connection diagnostics.

Suggested states:

```text
disabled
unprovisioned
connecting
connected
disconnected
authentication_failed
configuration_error
```

Use bounded exponential backoff with jitter.

The cloud connection must not prevent startup of the lighting engine.

## 9.3 CloudMqttService

Responsibilities:

- MQTT connection.
- TLS validation.
- Authentication.
- Topic subscriptions.
- Message publication.
- Incoming message queue.
- Last Will and Testament.
- Session management.
- Connection diagnostics.

Use separate cloud settings and secrets.

Do not reuse `mqtt.json` or the existing `mqtt` NVS namespace for cloud credentials.

Introduce a distinct configuration document and secret namespace.

## 9.4 CloudCommandRouter

Incoming messages must be validated before execution.

Validation:

1. Payload length.
2. JSON parsing.
3. Schema version.
4. Command ID.
5. Expiration.
6. Device identity.
7. Target light, room, group or scene.
8. Operation support.
9. Parameter ranges.
10. Deduplication.

After validation, enqueue the command through the existing Lumen command interface.

The MQTT callback must not access hardware or block.

## 9.5 State synchronization

Subscribe to existing lighting and application events.

When a light changes locally:

- Physical switch.
- Local web application.
- Existing MQTT integration.
- ESP-NOW peer.
- Local automation.
- Cloud command.

The cloud state publisher should receive the corresponding state event and publish the current reported state.

Avoid publishing every 20 ms lighting tick.

Publish meaningful state changes and coalesce rapid updates.

The reported state must represent actual or confirmed applied state, not just the requested state.

## 9.6 FreeRTOS integration

Respect the existing task registry.

The existing MQTT service already uses ESP-MQTT and a bounded message-handling design.

The new cloud service must not create uncontrolled tasks or allocate unbounded queues.

Document any new task, queue or timer in `docs/ARCHITECTURE.md`.

Never block the lighting task on network operations.

Do not take the model mutex from MQTT callbacks.

Use the established lock ordering and subsystem ownership rules.

## 9.7 Cloud offline behavior

When the internet is unavailable:

- Physical switches work.
- Local lighting works.
- Local scenes work.
- Local automations work.
- ESP-NOW synchronization works where configured.
- Local REST API works.
- Local WebSocket works.
- Existing local MQTT integration works when its broker is reachable.
- Cloud commands are unavailable.
- Cloud-only state becomes stale until reconnection.

Do not store old remote lighting commands for execution after reconnection.

Use bounded event buffering for eligible telemetry and events.

Configuration updates should use explicit revisions and synchronization.

---

# 10. Development Phase 6 — Gateway Connectivity

## 10.1 Gateway responsibilities

The Lumen Gateway connects local child controllers to the managed cloud.

It must:

- Maintain child-device registry.
- Communicate with children.
- Forward authorized commands.
- Receive child state.
- Forward child events.
- Report child availability.
- Maintain cloud connectivity.
- Buffer eligible messages during internet outages.
- Synchronize configuration.

## 10.2 Gateway routing

The cloud maintains an authoritative mapping between gateways and child controllers.

Example:

```json
{
  "gateway_id": "LUMEN-HUB-001",
  "children": [
    {
      "device_id": "LUMEN-CHILD-001",
      "route_epoch": 3
    },
    {
      "device_id": "LUMEN-CHILD-002",
      "route_epoch": 3
    }
  ]
}
```

The gateway must not impersonate devices that are not assigned to it.

Every forwarded message must preserve:

- Original device ID.
- Gateway ID.
- Message type.
- Sequence number.
- Route epoch.
- Authentication information.

## 10.3 ESP-NOW integration

Reuse the existing ESP-NOW discovery and synchronization implementation where possible.

Do not replace the existing ESP-NOW protocol without a demonstrated need.

Add gateway-specific message types or routing metadata through a versioned protocol extension.

Maintain compatibility with existing controller-to-controller lighting synchronization.

For an ESP32 gateway, implement cloud bridging on a controller with direct internet access.

A Raspberry Pi-based hub would need suitable ESP-NOW radio hardware or a separate ESP32 radio co-processor.

## 10.4 Gateway outage behavior

If the gateway loses internet:

- Children remain operational locally.
- Local lighting and automations continue.
- The gateway maintains local communication.
- Eligible telemetry/events are buffered.
- Expired commands are discarded.
- On recovery, the gateway reconnects and republishes current child state.

---

# 11. Development Phase 7 — Cloud REST API

Use `/api/v1` for the cloud API.

The cloud API is not the same API as the firmware's local REST API.

The cloud API manages users, homes, ownership, commands and remote device management.

## 11.1 Authentication

```http
POST /api/v1/auth/register
POST /api/v1/auth/login
POST /api/v1/auth/refresh
POST /api/v1/auth/logout
GET  /api/v1/auth/me
```

Use short-lived access tokens and rotating refresh tokens.

Mobile token storage must use platform-secure storage.

## 11.2 Home management

```http
GET    /api/v1/homes
POST   /api/v1/homes
GET    /api/v1/homes/{home_id}
PATCH  /api/v1/homes/{home_id}
DELETE /api/v1/homes/{home_id}

GET    /api/v1/homes/{home_id}/members
POST   /api/v1/homes/{home_id}/invitations
```

## 11.3 Rooms

```http
GET    /api/v1/homes/{home_id}/rooms
POST   /api/v1/homes/{home_id}/rooms
GET    /api/v1/rooms/{room_id}
PATCH  /api/v1/rooms/{room_id}
DELETE /api/v1/rooms/{room_id}
```

## 11.4 Devices

```http
GET    /api/v1/homes/{home_id}/devices
POST   /api/v1/devices/claim
GET    /api/v1/devices/{device_id}
PATCH  /api/v1/devices/{device_id}
DELETE /api/v1/devices/{device_id}

GET    /api/v1/devices/{device_id}/status
GET    /api/v1/devices/{device_id}/capabilities
GET    /api/v1/devices/{device_id}/diagnostics
```

## 11.5 Commands

```http
POST /api/v1/devices/{device_id}/commands
GET  /api/v1/commands/{command_id}
GET  /api/v1/devices/{device_id}/commands
```

Example request:

```json
{
  "operation": "light.set",
  "target": "living_room_main",
  "parameters": {
    "power": true,
    "brightness": 80
  },
  "ttl_seconds": 30
}
```

The API verifies the user and device permissions, records the command and routes it through MQTT.

## 11.6 Scenes

```http
GET    /api/v1/homes/{home_id}/scenes
POST   /api/v1/homes/{home_id}/scenes
GET    /api/v1/scenes/{scene_id}
PATCH  /api/v1/scenes/{scene_id}
DELETE /api/v1/scenes/{scene_id}
POST   /api/v1/scenes/{scene_id}/activate
```

## 11.7 Automations

```http
GET    /api/v1/homes/{home_id}/automations
POST   /api/v1/homes/{home_id}/automations
GET    /api/v1/automations/{automation_id}
PATCH  /api/v1/automations/{automation_id}
DELETE /api/v1/automations/{automation_id}
POST   /api/v1/automations/{automation_id}/enable
POST   /api/v1/automations/{automation_id}/disable
```

Use the existing firmware automation schema as the starting point.

Do not invent a separate incompatible automation format.

The cloud stores and distributes supported automation definitions. Local controllers execute their automations.

## 11.8 Gateway APIs

```http
GET  /api/v1/homes/{home_id}/gateways
POST /api/v1/gateways/claim
GET  /api/v1/gateways/{gateway_id}
GET  /api/v1/gateways/{gateway_id}/children
POST /api/v1/gateways/{gateway_id}/children/{device_id}
DELETE /api/v1/gateways/{gateway_id}/children/{device_id}
```

## 11.9 OTA APIs

```http
GET  /api/v1/ota/releases
POST /api/v1/ota/jobs
GET  /api/v1/ota/jobs/{job_id}
GET  /api/v1/devices/{device_id}/ota
```

## 11.10 Cloud WebSocket

Provide an authenticated WebSocket endpoint for mobile application events.

Example:

```text
wss://api.lumen.example/api/v1/events
```

Events:

```text
device.online
device.offline
device.state.updated
device.command.updated
device.config.updated
device.ota.progress
device.ota.completed
gateway.child.updated
```

The WebSocket is an API transport, not a cloud user interface.

The mobile app subscribes to events authorized for the authenticated user.

---

# 12. Development Phase 8 — Mobile Application

## 12.1 Technology

Use:

- React Native.
- Expo.
- TypeScript.
- Expo Router.
- TanStack Query.
- Zustand.
- Zod.
- React Hook Form.
- Secure storage.
- BLE support.
- QR scanning.
- WebSocket client.
- Push notification integration.

The mobile app must consume the cloud API.

It must not connect directly to the cloud database.

It must not receive permanent MQTT device credentials.

## 12.2 Main screens

```text
Authentication
    |
    +-- Login
    +-- Register
    +-- Password recovery

Home
    |
    +-- Home dashboard
    +-- Rooms
    +-- Devices
    +-- Scenes
    +-- Automations
    +-- Gateways
    +-- Notifications
    +-- Settings

Device management
    |
    +-- Device details
    +-- Connection status
    +-- Firmware version
    +-- Diagnostics
    +-- OTA update

Provisioning
    |
    +-- QR scanner
    +-- BLE discovery
    +-- Wi-Fi setup
    +-- Claim device
    +-- Assign room
```

## 12.3 Lighting controls

Support the existing Lumen light types:

- Switch.
- Dimmer.
- RGB.
- RGBW.
- CCT.

Use the existing firmware light data model and API semantics as the reference.

Do not duplicate the firmware's hardware-specific configuration logic in the mobile application.

## 12.4 Local and remote control

The mobile app should automatically choose the available communication route.

### Local

When the controller is reachable on the home LAN:

- Use firmware REST API.
- Use firmware WebSocket for live updates.
- Authenticate with the local controller.
- Prefer local communication.

### Remote

When outside the home network:

- Use cloud HTTPS API.
- Use cloud WebSocket events.
- Cloud routes commands over MQTT.
- Receive state updates through cloud events.

### Route selection

```text
Open application
       |
       v
Find registered home devices
       |
       v
Check local controller availability
       |
       +------ Local available ------> Local API + WS
       |
       +------ Local unavailable ---> Cloud API + WS
```

For gateway children, local control should use the gateway or local network route.

Do not present cached device state as current state.

Show whether the current connection is local, remote or unavailable.

## 12.5 Mobile command state

Maintain three distinct concepts:

- Reported state.
- Desired state.
- Pending command.

Example:

```text
Customer presses ON
        |
        v
Show pending state
        |
        v
Send cloud API request
        |
        v
Cloud routes MQTT command
        |
        v
Controller executes
        |
        v
Controller publishes state
        |
        v
Cloud WebSocket event
        |
        v
Mobile app confirms actual state
```

If the command expires or fails, show the actual reported state and the failure reason.

---

# 13. Configuration Synchronization

Cloud configuration and local controller configuration must be synchronized deliberately.

Do not allow arbitrary cloud configuration to overwrite the hardware profile.

## 13.1 Configuration authority

| Configuration | Authority |
|---|---|
| Device identity | Manufacturing/device |
| Hardware profile | Device/manufacturing |
| Pin mapping | Device |
| Room assignments | Cloud |
| Friendly names | Cloud |
| Light configuration | Cloud, validated by device |
| Scenes | Cloud, cached locally |
| Automations | Cloud, executed locally |
| Actual light state | Device |
| Firmware version | Device |

## 13.2 Revision model

Example:

```json
{
  "schema_version": 1,
  "config_revision": 42,
  "device_id": "LUMEN-00000001",
  "configuration": {
    "timezone": "Asia/Colombo",
    "room_id": "room_living",
    "lights": [],
    "scenes": [],
    "automations": []
  }
}
```

Track:

- Desired revision.
- Reported revision.
- Last applied revision.
- Configuration status.

Use the existing configuration manager and migration architecture where appropriate.

Validate every incoming document before applying it.

If validation fails, preserve the last working configuration.

---

# 14. OTA Management

Preserve the existing firmware dual-slot OTA architecture.

Add cloud orchestration for:

- Firmware release registration.
- Hardware model compatibility.
- Stable and beta release channels.
- Device update jobs.
- Update progress.
- Update results.
- Rollback reporting.

Firmware artifacts must be stored in object storage and downloaded over HTTPS.

MQTT should carry OTA job notifications and metadata, not firmware binaries.

Every firmware artifact must have a verified hash and cryptographic signature.

Only compatible and correctly signed firmware may be installed.

The cloud must never force an incompatible firmware onto a controller.

---

# 15. Security Requirements

## 15.1 Cloud security

- HTTPS for all API requests.
- TLS for MQTT.
- Authentication for every device.
- Authentication for every mobile user.
- Authorization for every home and device.
- Per-device MQTT ACLs.
- Gateway child-device authorization.
- Rate limiting.
- Audit logs.
- Credential revocation.
- Secure secret storage.
- Database backups.

## 15.2 Firmware security

Plan production hardening for:

- Secure Boot V2 where supported.
- Flash Encryption.
- Signed OTA.
- Secure credential storage.
- Unique manufacturing credentials.
- Protected provisioning mode.
- Credential rotation.
- Recovery procedures.

Do not enable irreversible eFuse settings until manufacturing and recovery procedures have been tested on each target chip.

## 15.3 Local API security

The current firmware uses cookie-based sessions and CSRF protection.

Do not assume the cloud JWT authentication model can simply be reused for the local API.

Design a separate, explicit mobile-to-local-controller authentication flow.

Never expose the local controller's administrative credentials to the cloud.

---

# 16. Testing Strategy

## 16.1 Existing firmware regression tests

Before and after cloud changes, run:

```bash
pio test -e native
pio run -e esp32
pio run -e esp32s3
```

Run the existing web UI tests and firmware integration suites.

The existing local MQTT and Home Assistant integration must continue working.

## 16.2 Cloud tests

Test:

- User registration and login.
- Home creation.
- Device claiming.
- Device ownership.
- MQTT authentication.
- MQTT topic ACLs.
- Remote command routing.
- Duplicate commands.
- Expired commands.
- Command acknowledgement.
- State synchronization.
- Gateway assignment.
- Gateway authorization.
- OTA job creation.
- Credential revocation.

## 16.3 Mobile tests

Test:

- Login and refresh token lifecycle.
- QR scanning.
- BLE provisioning.
- Local controller discovery.
- Remote control.
- Local/remote route switching.
- State synchronization.
- Offline behavior.
- Push notifications.
- Multiple homes and rooms.

## 16.4 Physical integration testing

Initial pilot setup:

- Two direct-connected Lumen controllers.
- One gateway-capable controller.
- Two ESP-NOW child controllers.
- Physical relays or dimmers.
- One Android device.
- One iOS device if available.

Test:

1. Local control.
2. Remote control.
3. Internet outage.
4. Cloud broker outage.
5. Gateway internet outage.
6. Gateway reboot.
7. Controller reboot.
8. Duplicate MQTT messages.
9. Expired commands.
10. Wi-Fi disconnection.
11. Credential revocation.
12. Firmware update and rollback.

Local lighting must remain functional throughout cloud outages.

## 16.5 Existing hardware verification limitations

Do not treat compile success as physical hardware verification.

The repository currently records:

- KC868-AP reference hardware testing.
- Generic ESP32 GPIO/LEDC and ESP-NOW testing.
- ESP32-S3 compile testing without physical S3 validation.
- Remaining KC868-AP relay polarity, Ethernet and RS485 verification work.

Cloud implementation must not advertise hardware capabilities that have not been physically validated.

---

# 17. Development Roadmap

Implement in the following order.

| Phase | Deliverable | Priority |
|---|---|---|
| 0 | Repository audit and protocol design | Mandatory |
| 1 | Cloud infrastructure and EMQX | Mandatory |
| 2 | Cloud database and device registry | Mandatory |
| 3 | Device identity and provisioning | Mandatory |
| 4 | MQTT protocol and CloudMqttService | Mandatory |
| 5 | Direct remote lighting control | Mandatory |
| 6 | Cloud REST API and WebSocket events | Mandatory |
| 7 | Mobile application and onboarding | Mandatory |
| 8 | Gateway and ESP-NOW bridge | Mandatory |
| 9 | Configuration synchronization | Mandatory |
| 10 | OTA orchestration | Commercial release |
| 11 | Security hardening and pilot testing | Commercial release |

Do not start the mobile UI before the API contracts and device identity model are stable.

Do not start gateway development before direct MQTT control is working.

Do not start OTA orchestration before the basic cloud command and state synchronization is proven.

---

# 18. First Commercial Release Scope

The first commercially meaningful version should support:

## Firmware

- Dedicated CloudMqttService.
- Device identity.
- Secure cloud authentication.
- Direct MQTT connectivity.
- Cloud command routing.
- Reported state publishing.
- Device availability.
- Offline local operation.

## Cloud

- User registration and login.
- Home management.
- Device claiming.
- Device registry.
- MQTT authentication and ACLs.
- Remote command service.
- Device state service.
- Cloud WebSocket events.
- Gateway registration and authorization.

## Mobile

- Login and registration.
- Home dashboard.
- QR-based device onboarding.
- BLE Wi-Fi provisioning.
- Rooms and device list.
- Switch and dimmer control.
- RGB and CCT control.
- Scene activation.
- Remote control.
- Device online/offline status.
- Local/remote connection indicator.

## Gateway

- Gateway identity.
- Child registration.
- ESP-NOW communication.
- Cloud message forwarding.
- Child state synchronization.
- Offline local operation.

Defer advanced analytics, subscriptions, complex automatic failover and large-scale multi-region deployment.

---

# 19. Definition of Done

The first release is complete when a customer can:

1. Install a Lumen controller.
2. Open the Lumen mobile application.
3. Create an account.
4. Create a home.
5. Scan and claim a Lumen controller.
6. Configure its Wi-Fi.
7. Control its lights locally.
8. Leave the home and control the same lights remotely.
9. See updated light states in the mobile application.
10. Manage multiple controllers from one home.
11. Operate controllers through a Lumen gateway.
12. Continue controlling lights locally when the internet is disconnected.
13. Reconnect to the cloud without losing configuration or local automation.
14. Update firmware through the managed cloud workflow.

The cloud must never be required for ordinary local lighting operation.

---

# 20. Development Instructions for Coding Agents

Any coding agent working on this project must follow these rules.

1. Read the existing `AGENTS.md`, `SPEC.md` and `SKILL.md`.
2. Inspect the current implementation before modifying it.
3. Preserve existing Lumen features and APIs.
4. Never replace the existing `MqttService`.
5. Implement a separate `CloudMqttService`.
6. Reuse the existing lighting command queue.
7. Reuse the existing event bus.
8. Reuse the existing configuration and migration system.
9. Reuse the existing ESP-NOW implementation where appropriate.
10. Never access GPIO directly from cloud networking modules.
11. Never block the lighting task.
12. Never store credentials in plaintext configuration files.
13. Never expose permanent MQTT device credentials to the mobile app.
14. Keep cloud API schemas versioned.
15. Keep MQTT payload schemas versioned.
16. Test each implementation phase before moving forward.
17. Update `docs/ARCHITECTURE.md`.
18. Update `docs/IMPLEMENTATION_STATUS.md`.
19. Add unit and integration tests for new functionality.
20. Do not claim physical hardware support without actual hardware testing.

## Final architectural rule

**Lumen Firmware owns the physical lighting system. Lumen Cloud owns remote identity, access and message routing. Lumen Mobile App owns the customer experience.**

The cloud must remain API-only. The mobile app is the customer's interface to the managed cloud, while the existing firmware web application remains the local advanced configuration interface.

This separation is fundamental to the long-term maintainability and commercial scalability of Lumen.