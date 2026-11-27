# P4 — Commands, state, availability, events WebSocket

## Outcome

The cloud routes commands to devices and tracks them to a device-level
result. It ingests state, config and availability, and pushes them to apps
in real time. Everything is proven end to end with `cloud/tools/simgateway`, a
protocol-correct simulated gateway with simulated children.

## Tasks

- [x] `app/mqtt/bridge.py`: a single aiomqtt service connection per
      instance (shared subscriptions `$share/lumen/…` so N replicas split
      the load). It reconnects with backoff and has bounded internal
      queues.
- [x] Ingest:
  - [x] `availability` and `state` go to the `device_state` table:
        reported state JSON, revision, `last_seen_at`, online.
  - [x] `config/reported` goes to `device_config`: revision and document.
        It also updates `devices.fw_version` and the capabilities.
  - [x] `cmd/result` goes to `commands`.
  - [x] `events` go to an `events` table (bounded retention, 30 days).

  Every payload is validated against the protocol models. Invalid payloads
  are counted and dropped.
- [x] Commands:
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
- [x] Offline device: a command for a device that is offline (or whose
      gateway is) is refused with `409 DEVICE_OFFLINE`. It is never queued
      (brief §9.7).
- [x] Events WebSocket `/api/v1/events`:
  - authenticated with the access token (first message or query);
  - fanned out across instances through Redis pub/sub;
  - filtered to the user's homes;
  - heartbeat every 25 s;
  - a slow consumer is disconnected once its bounded send buffer is full.
- [x] Gateway topics: `g/{gw}/status` and availability ingest. When a
      gateway goes offline, all of its children are marked offline.
- [x] `cloud/tools/simgateway`: a CLI that:
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

## Result (2026-11-27)

Gate passed. 378 cloud tests (45 new unit, 7 live, 64 protocol-model contract); ruff and mypy --strict clean.
Live suite (compose: EMQX 5.8.9, two API replicas, simgateway) passed 6 runs in a row after the fixes below:
command → `applied` over the WebSocket median 71–81 ms, p95 82–83 ms (gate: < 300 ms); duplicate delivery
executed once; an expired command never applied; offline controller refused (409); a gateway whose
connection drops → `device.offline` for it and its children in < 1 s via the Last Will; the retained children
list is published; with two replicas each command reached the gateway exactly once and events reached a
WebSocket on the other replica (local docker).

Bugs found by the live suite and fixed:
- Deadlock between deleting a home and ingesting its devices' messages (opposite lock order). Ingestion now
  locks the device row first and retries deadlocks/serialisation failures (3 attempts).
- A fast device's `applied` could be overwritten by the API's own `sent` update (and later "expired" by the
  sweeper). Commands are now stored as `sent` before publishing; nothing is written after the publish.
- Latency spikes (p95 up to ~950 ms) from WAL-flush contention: ingestion transactions use
  `synchronous_commit = off` (device reports are re-sent; commands and accounts stay durable).
- The simulator's crash mode closed a socket under the MQTT library; it now uses `shutdown()`.

Deviations:
- `config.apply` is refused (400 `NOT_AVAILABLE`) until P8; the protocol model already validates it.
- Silent connection loss is detected after up to 2 × keepalive on EMQX (measured 10.0 s at 5 s), not 1.5 ×.
- The 24 h idempotency window is "keys are never reused" (unique per user): keys are UUIDs from the app.
- Each API instance's first broker connection attempts are refused until its own internal hook port is up
  (it shares the process); the bridge's backoff connects within a few seconds.
