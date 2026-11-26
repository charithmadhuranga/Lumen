# ESP-NOW Protocol — version 1

Multi-controller synchronisation between Lumen controllers (SPEC §12, Phase 11).
Implementation: `src/networking/espnow_protocol.cpp` (envelope, host-tested) and
`src/networking/espnow_manager.cpp` (radio, peers, sync). Verified on two boards
(KC868-AP + ESP32 DevKit, 2026-11-26).

## Model

- Every light is **owned** by exactly one controller (the one wired to it).
- Owners broadcast the **applied** state of their lights (`STATE`) on change (coalesced, ≤ 1 per light per 50 ms)
  and in full every 30 s and whenever a new peer appears.
- To change a remote light, a controller sends `COMMAND` addressed to the owner. The owner validates it and
  enqueues it like any local command (origin `espnow:<peer id>`), then broadcasts the resulting state.
- `SCENE` asks every controller to activate **its own** scene with that id (controllers without it ignore it).
- Commands are never forwarded and state is only sent by its owner, so synchronisation loops cannot form.

## Transport

- ESP-NOW v1 broadcast (`ff:ff:ff:ff:ff:ff`) on the current Wi-Fi channel: **all controllers must use the same
  channel**, i.e. be connected to the same access point. No per-peer ESP-NOW registration (no hardware peer limit),
  no link-layer encryption — authentication is the frame HMAC below.
- Broadcasts are not acknowledged. The sender waits for the send-complete callback before the next frame (frames
  queued faster than the radio sends them are dropped), and the 30 s full-state refresh heals lost STATE frames.
  Measured: 100 % of 300 commands delivered in a 20 s two-way burst; idle traffic ≈ 0.2 frames/s per controller.
- While ESP-NOW runs, Wi-Fi modem sleep is disabled (a dozing radio only hears broadcasts at beacon intervals).

## Frame (little-endian, ≤ 250 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `L` `N` |
| 2 | 1 | version = 1 (other versions are rejected and counted) |
| 3 | 1 | type: 1 HELLO, 2 STATE, 3 COMMAND, 4 SCENE |
| 4 | 1 | flags: bit 0 = timestamp valid (sender clock synchronised) |
| 5 | 1 | reserved = 0 |
| 6 | 4 | network tag = first 4 bytes of HMAC-SHA256("lumen-espnow-network", network id) |
| 10 | 4 | sequence: `boot count << 20 \| counter` — strictly increasing per sender, also across reboots |
| 14 | 4 | message id (random, duplicate detection) |
| 18 | 4 | timestamp, unix seconds (0 if not synchronised) |
| 22 | 2 | payload length (≤ 210) |
| 24 | n | payload, compact JSON |
| 24+n | 16 | HMAC-SHA256(network key, bytes 0 … 24+n−1), truncated to 16 bytes |

## Payloads

| Type | Payload | Notes |
|---|---|---|
| HELLO | `{"id":"lumen-0798","ip":"10.0.0.64","v":"0.1.0","lc":4}` | every 5 s; a peer is offline after 20 s without frames |
| STATE | `{"l":"ceiling","n":"Ceiling","t":"dimmer","p":1,"b":40[,"c":[r,g,b,w]][,"k":2700]}` | applied output |
| COMMAND | `{"to":"lumen-e704","l":"desk_led", …HA/MQTT JSON fields…}` | `state` ON/OFF/TOGGLE, `brightness`, `color`, `white`, `color_temp` (K), `transition` (s), `effect` |
| SCENE | `{"s":"evening"}` | |

## Validation and security (receiver)

1. Size, magic, version, length consistency, type → else dropped (`malformed` / `versionMismatch`).
2. Network tag must match ours → else `otherNetwork`.
3. HMAC with the shared network key (constant-time compare) → else `authFailed`.
4. Replay filter (16 senders, 32 recent message ids): duplicate message id → `duplicates`; sequence not above the
   sender's last → `replayed`; both clocks valid and |timestamp − now| > 120 s → `stale`; sender table full → `peersFull`.
5. Payload must be a JSON object; command fields validated like MQTT (`brightness` 0–100 …).
6. **Conflict rule:** a COMMAND whose timestamp is older than the last *local* (non-ESP-NOW) command on that light
   is rejected (`commandsRejected`, logged) — local physical input and local UI win over stale synchronised commands.

Limits: 8 peers, 32 remote lights per peer, receive queue 16 frames (overflow counted as `rxQueueFull`).
The network key (16–64 characters) lives in NVS, is generated on first enable, is shown only to a signed-in user
(`GET /integrations/espnow/key`) and is never in backups, exports or logs.

## Known limitations

- A receiver that reboots forgets the last sequence numbers; replays of frames recorded before its reboot are then
  only rejected by the ±120 s timestamp window (needs SNTP on both controllers).
- MQTT network-wide topics (SPEC §12.4 `lighting/network/…`) are not implemented; controllers on different Wi-Fi
  channels cannot sync over ESP-NOW.
- Roles (primary/secondary/gateway) are not implemented; every controller is a standalone owner of its lights.

## Testing

- Host: `test/test_host_espnow` — round trip, size limit, tampering, wrong key / network, malformed and lying
  lengths, replay, duplicates, stale window, sender limit, sequence continuity across reboots.
- Hardware: `test/integration/multi_test.py <A> <B> <password>` — discovery, remote commands both ways (PCA9685
  read-back), state sync, network scene, wrong key / other network rejected, ESP-NOW with the MQTT broker stopped,
  two-way burst without duplicates or amplification, peer restart and rejoin.
