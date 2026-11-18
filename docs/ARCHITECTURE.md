# Architecture

> Status: skeleton (Phase 0). Diagrams follow SPEC §4.1; tables fill in as
> phases land. Update this file whenever layers, tasks or the event bus
> change (AGENTS.md §12).

## 1. Layer diagram (SPEC §4.1)

```text
                    USER
                     |
                     v
             REACT WEB APPLICATION
                     |
              HTTP REST / WS
                     |
                     v
              WEB API SERVER
                     |
                     v
              APPLICATION CORE
                     |
          +----------+----------+
          |          |          |
          v          v          v
      LIGHTING   AUTOMATION   DEVICE
       ENGINE      ENGINE    REGISTRY
          |          |          |
          +----------+----------+
                     |
                  EVENT BUS
                     |
          +----------+-----------+
          |          |           |
          v          v           v
       HARDWARE   NETWORKING   STORAGE
       MANAGER     MANAGER     MANAGER
          |          |
          |      +---+----+------+
          |      |        |      |
          v      v        v      v
        GPIO    MQTT    ESP-NOW TCP/UDP
        PWM      HA
        RELAY
        RMT
        I2C
        RS485
```

## 2. Boundary rules (summary; full text in AGENTS.md §4)

- Application modules never touch GPIO; everything goes through HAL
  interfaces and board profiles.
- HTTP/MQTT/WebSocket/ESP-NOW callbacks only enqueue commands — they never
  run hardware work directly.
- Subsystems communicate through the internal event bus.
- Persistence is transactional; migrations are never destructive.
- The lighting engine works without networking; automations run locally.

## 3. Task registry

AGENTS.md §5: every FreeRTOS task is documented here — owner, priority,
stack. Rows are added the moment the task exists.

| Task | Owner module | Priority | Stack | Role |
|---|---|---|---|---|
| `main` (ESP-IDF) | `src/main.cpp` | 1 (ESP-IDF default) | 3584 B (default) | Phase 0 boot banner + 5 s heartbeat. Replaced by service tasks in Phase 1. |

Configured limits: no uncontrolled task creation (AGENTS.md §5); queue
bounds and timeouts are documented per service as they land.

## 4. Event bus (overview — implemented in Phase 1)

- Typed publish/subscribe channels; producers enqueue, consumers process in
  their own task context.
- No blocking hardware work inside bus callbacks.
- Subscribers: lighting engine, networking, storage, diagnostics (grows per
  phase).

## 5. Boot order (Phase 0)

1. 2nd stage bootloader → partition table check (dual OTA + LittleFS).
2. `app_main`: chip/flash/heap banner via structured logs (`tag: lumen`).
3. Heartbeat loop (Phase 0 only).

Phase 1 replaces step 3 with the SystemManager service lifecycle (init →
start → healthy), documented here when it lands.

## 6. Storage layout

| Partition | Size | Purpose |
|---|---|---|
| `nvs` | 24 KB | Secrets and critical settings only (AGENTS.md §8) |
| `otadata` | 8 KB | OTA slot selection |
| `phy_init` | 4 KB | RF calibration data |
| `ota_0` / `ota_1` | 1375 KB each | Application slots (dual OTA) |
| `storage` | 1125 KB | LittleFS: `/config` JSON + gzipped web assets (budget ≤ 1.0 MB, DESIGN §14.1) |

Changing `partitions.csv` requires asking first (AGENTS.md §15); the layout
is enforced by the host test `test/test_host_partitions`.
