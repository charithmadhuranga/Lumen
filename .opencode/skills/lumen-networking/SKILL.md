---
name: lumen-networking
description: Networking and multi-controller work for Lumen — Wi-Fi STA/AP/fallback, Ethernet PHY profiles, MQTT with Home Assistant discovery, TCP framing, UDP discovery/control, RS485/Modbus transport, ESP-NOW peer sync with versioned binary envelopes, and command conflict resolution across controllers. Use for anything under src/networking/, integration endpoints, or the Integrations/Network/Controllers UI. Triggers: "wifi", "mqtt", "home assistant", "esp-now", "tcp", "udp", "ethernet", "rs485", "multi controller", "sync", "phase 10", "phase 11".
---

# ESP Lighting — Networking & Multi-Controller

**Plans:** `.opencode/plans/phase-10-networking.md`,
`phase-11-multi-controller.md`.
**Spec:** SPEC §12 (sync), §13 (MQTT/HA), §14 (TCP/UDP), §15 (network mgr),
§16.4 (security).

## Universal networking rules

- **Callbacks enqueue, workers execute.** No hardware work, no blocking, no
  JSON churn inside an MQTT/ESP-NOW/TCP/UDP callback (AGENTS.md §8).
- Validate everything on receipt: size bound, protocol version, then
  authentication — in that order. Count and log rejects.
- Every protocol is versioned; every buffer is bounded.
- Reconnection everywhere; **lighting and local automation keep working with
  all networks down** — this is an acceptance test, not a hope.
- Credentials live in NVS; integration JSON documents never contain secrets
  (write-only API fields: "•••••• (set)" + Replace).
- Never log credentials or sensitive payloads.

## Phase 10 specifics

- **Wi-Fi/Ethernet:** STA (DHCP/static, backoff reconnection), secure AP
  provisioning (unique device identity — never an unauthenticated permanent
  hotspot), AP+STA, configurable fallback policy; Ethernet only when the
  board profile declares it (LAN8720: addr 0, MDC 23, MDIO 18, CLK
  GPIO17_OUT, power -1 for KC868-AP — profile-driven, no hardcoded pins).
- **MQTT:** broker/TLS/QoS/keepalive/LWT (`lighting/{controller_id}/status`),
  topics per SPEC §13.2, JSON payloads both directions, state published only
  **after** hardware application, retained refresh on reconnect.
- **HA discovery:** correct schema + optional properties per light type,
  stable `unique_id`s, config regenerated when the entity changes, retained
  deletion on delete/rename, availability = LWT.
- **TCP:** explicit message framing — a read is never assumed to be a
  message (length-prefix or delimiter; versioned; fragmented/coalesced reads
  are test cases). Auth handshake, heartbeats, timeouts, multi-client.
- **UDP:** discovery/events/announcements; control **disabled or
  authenticated by default**; packet validation + rate limiting.
- **RS485:** UART from profile + Modbus RTU framing; direction-control
  behavior comes from Phase 3's measurement, never a guess.

## Phase 11 specifics (multi-controller)

- **ESP-NOW envelope:** compact bounded binary (the SPEC §12.3 JSON is an
  illustration only): version byte, message id, source/dest, type, sequence,
  timestamp, payload; max-size validated; malformed → drop + counter.
  Document the format in `docs/ESP_NOW_PROTOCOL.md` as you build it.
- **Security:** application-layer message authentication + replay protection
  (sequence/timestamp window) + duplicate detection. Link-layer encryption is
  not authentication (SPEC §16.4).
- **Peers:** registration/lifecycle with a real peer limit (surface the
  limit from the API — don't assume), discovery, heartbeat/last-seen,
  channel config (must match the Wi-Fi AP channel — handle explicitly).
- **Conflict resolution (SPEC §12.5):** every accepted command has a unique
  id, origin controller and sequence. Stale rejected, duplicates ignored,
  newer command replaces an active transition, **local physical input wins**,
  state published only after hardware application, origin-echo suppression
  prevents reconciliation loops (test with two controllers).
- **Roles:** standalone / primary / secondary / gateway — standalone
  operation must keep working while participating.
- **MQTT network sync topics:** SPEC §12.4 — same enqueue-only path.

## UI

Integrations cards (MQTT, HA, ESP-NOW, TCP, UDP) with enable + settings +
Test connection; Network screen (Wi-Fi scan/AP/Ethernet, disconnect-warning
dialog); Devices → Controllers tab (peers, role, last seen, quality). WS
events `controller_discovered` / `controller_disconnected`.

## Validation gate

- [ ] Protocols independent; any one disabled leaves the rest working.
- [ ] Broker + Wi-Fi killed → local lights/scenes/automations continue; MQTT
      resumes on return (recorded test).
- [ ] HA lifecycle: create, rename, delete each verified against a broker.
- [ ] TCP framing: fragmented, coalesced and oversized reads tested.
- [ ] UDP control authenticated/disabled by default; rate limiter tested.
- [ ] ESP-NOW: envelope round-trip, oversize/malformed/version mismatch,
      replay, duplicate, stale, loop-soak tests green.
- [ ] ≥2 physical controllers synchronized (date + boards recorded in
      `docs/IMPLEMENTATION_STATUS.md`); local input responsive during sync.
- [ ] Secrets absent from responses/logs/exports (test).
- [ ] `docs/ESP_NOW_PROTOCOL.md` complete and versioned.

## Pitfalls

- Publishing state from a callback before hardware applied (creates echo
  loops and false states).
- Assuming ESP-NOW peer counts or radio coexistence "just work".
- TCP parser that assumes one JSON per read.
- TLS heap surprise — measure free heap with TLS enabled before shipping.
