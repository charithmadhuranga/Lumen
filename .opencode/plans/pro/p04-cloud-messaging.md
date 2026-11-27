# P4 — Commands, state, availability, events WebSocket

## Outcome

The cloud routes commands to devices and tracks them to a device-level
result. It ingests state, config and availability, and pushes them to apps
in real time. Everything is proven end to end with `cloud/tools/simgateway`, a
protocol-correct simulated gateway with simulated children.

## Tasks

- [ ] `app/mqtt/bridge.py`: a single aiomqtt service connection per
      instance (shared subscriptions `$share/lumen/…` so N replicas split
      the load). It reconnects with backoff and has bounded internal
      queues.
- [ ] Ingest:
  - [ ] `availability` and `state` go to the `device_state` table:
        reported state JSON, revision, `last_seen_at`, online.
  - [ ] `config/reported` goes to `device_config`: revision and document.
        It also updates `devices.fw_version` and the capabilities.
  - [ ] `cmd/result` goes to `commands`.
  - [ ] `events` go to an `events` table (bounded retention, 30 days).

  Every payload is validated against the protocol models. Invalid payloads
  are counted and dropped.
- [ ] Commands:
  - `POST /devices/{id}/commands`, with role checks:
    - guest and member: `light.*`, `group.set`, `room.set`,
      `scene.activate`, `device.status`;
    - admin and owner: also `device.restart` and `config.apply`.
  - Arguments are validated against the device's reported capabilities
    (no colour command for a dimmer).
  - Stored, then published with MQTT 5 message expiry. The TTL defaults to
    15 s, with a maximum of 60 s.
  - `GET /commands/{id}` and `GET /devices/{id}/commands`.
  - A sweeper marks `sent` commands past `exp` as `expired`.
  - Idempotency: `Idempotency-Key` header, 24 h window.
- [ ] Offline device: a command for a device that is offline (or whose
      gateway is) is refused with `409 DEVICE_OFFLINE`. It is never queued
      (brief §9.7).
- [ ] Events WebSocket `/api/v1/events`:
  - authenticated with the access token (first message or query);
  - fanned out across instances through Redis pub/sub;
  - filtered to the user's homes;
  - heartbeat every 25 s;
  - a slow consumer is disconnected once its bounded send buffer is full.
- [ ] Gateway topics: `g/{gw}/status` and availability ingest. When a
      gateway goes offline, all of its children are marked offline.
- [ ] `cloud/tools/simgateway`: a CLI that:
  - registers through a gateway claim and connects with TLS;
  - registers N simulated children;
  - answers commands (with configurable latency and failures) and
    publishes state and config.

  It is used by the tests and as the reference for the Go gateway's
  cloud side.

## Tests

- **Unit:** each ingest validator, command state machine and permission
  path.
- **Integration, against compose:**
  - Gateway claim → connect → child registration → command → `applied`
    → state → WebSocket event, with the latency recorded.
  - Duplicate delivery executes once.
  - An expired command is never applied (simgateway checks `exp`).
  - Offline gives 409.
  - The gateway's LWT gives `device.offline` for every child within the
    keepalive × 1.5.
  - Two API replicas: a command is handled once and events reach clients
    connected to either replica.

## Gate

- The integration suite passes against compose.
- Command round-trip latency (API → `applied` result → WebSocket) is under
  300 ms at the 95th percentile, locally.
