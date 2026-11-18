# Phase 11 — Multi-Controller Synchronization

**SPEC §26 Phase 11, SPEC §12** · **Skill:** `lumen-networking`

## Outcome

Two or more controllers behave as one logical lighting system over ESP-NOW
(low latency, local) and MQTT (network-wide), with explicit conflict
resolution. MQTT outage does not disable ESP-NOW; offline local operations
continue; duplicates and sync loops are impossible.

## Dependencies

Phase 10 (MQTT), Phase 4 (command/origin model), Phase 1 (event bus).
Requires ≥2 physical ESP32 boards for real verification (one can be a
generic DevKit).

## Tasks

### 11.1 ESP-NOW manager (`src/networking/espnow_manager.*`)

- [ ] Peer registration/lifecycle with a **peer limit** (do not assume
      unlimited peers), controller identification, device discovery,
      heartbeat with last-seen tracking, network status (SPEC §12.3).
- [ ] **Versioned compact binary envelope** (the SPEC §12.3 JSON is a
      illustration only): bounded fields, explicit version byte, message id,
      source/destination, type, sequence, timestamp, payload — max packet
      size validated on RX, malformed packets dropped and counted.
- [ ] Security at the application layer: message authentication, replay
      protection (sequence + timestamp window), duplicate detection —
      ESP-NOW link encryption alone is not authentication (SPEC §16.4).
- [ ] Channel configuration, retry policy where appropriate (ack'd
      command types vs fire-and-forget telemetry).
- [ ] Commands forwarded between controllers: state sync, group sync, scene
      sync, command forwarding — all through the same enqueue path as local
      producers with `origin` set.

### 11.2 MQTT network synchronization

- [ ] Network topics from SPEC §12.4:
      `lighting/network/{network_id}/controllers/{id}/status`,
      `.../events`, `.../lights/{id}/set|state`, `.../scenes/{id}/activate`.
- [ ] Same payload validation and enqueue-only rule as local MQTT commands.

### 11.3 Conflict resolution (SPEC §12.5) — the core of this phase

- [ ] Every accepted command carries: unique command id, origin controller,
      sequence number.
- [ ] Rules enforced and unit-tested: stale commands rejected; duplicates
      ignored; a newer accepted command replaces an active transition; local
      physical input processed immediately and wins over stale synchronized
      commands; actual state published only after hardware application.
- [ ] Loop prevention: reconciliation messages are tagged so a controller
      never re-broadcasts state it originated (origin echo suppression) —
      prove with a two-controller loop test.
- [ ] Roles: standalone / primary / secondary / gateway (SPEC §12.2) — a
      controller works standalone while participating in a network.

### 11.4 Web UI

- [ ] Devices → **Controllers** tab (DESIGN §7.7): discovered peers, role,
      last seen, link quality, online/offline status.
- [ ] Integration card for ESP-NOW (enable, channel/network id, security key
      write-only field, peer list) — now real, replacing nothing (it was not
      shipped in Phase 10).
- [ ] WS events: `controller_discovered`, `controller_disconnected`.

### 11.5 Tests

- [ ] Host: envelope encode/decode round-trip, oversize/malformed/version-
      mismatch rejection, replay (same seq), duplicate suppression, stale
      rejection, origin-echo suppression (loop test), peer-limit enforcement.
- [ ] Two-controller integration on real hardware: light command on A
      appears on B <250 ms over ESP-NOW; MQTT sync across the network;
      pull broker → ESP-NOW still syncs; power-cycle one controller →
      rejoin works; duplicate/loop soak test (SPEC §11 acceptance).
- [ ] Offline behavior suite from SPEC §12.6 re-run in this context.
- [ ] Playwright: controllers tab against mock peers; axe.

## Contracts & interfaces introduced

- Binary packet format + sequence/auth rules → **`docs/ESP_NOW_PROTOCOL.md`**
  (version, fields, limits, security) — this doc is a Phase 11 deliverable,
  not an afterthought.
- Conflict-resolution policy documented in `docs/ARCHITECTURE.md`.

## Gate (acceptance criteria)

1. ≥2 supported controllers synchronize lighting (real hardware run recorded
   in `docs/IMPLEMENTATION_STATUS.md` with date + boards used).
2. MQTT outage does not disable ESP-NOW; local automation and lighting
   continue in all-offline conditions.
3. Duplicate commands and synchronization loops prevented (automated test +
   soak).
4. Local physical input stays responsive during sync traffic (measured).
5. Envelope validation rejects malformed/oversized/replayed packets with
   counters visible in diagnostics.
6. `docs/ESP_NOW_PROTOCOL.md` complete and versioned.

## Docs to update

`docs/ESP_NOW_PROTOCOL.md`, `docs/API.md` (controllers endpoints + WS
events), `docs/ARCHITECTURE.md`, `docs/UI_UX.md`,
`docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- ESP-NOW + Wi-Fi coexistence on one radio: channel must match the AP
  channel — handle explicitly and document.
- Peer limits differ per ESP32 variant — surface a real limit from the API,
  not a guess.
