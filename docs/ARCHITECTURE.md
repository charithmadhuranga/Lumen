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
stack, core. No other task is created; per-request work never spawns tasks.

| Task | Owner module | Prio | Stack | Core | Role |
|---|---|---|---|---|---|
| `main` (ESP-IDF) | `src/main.cpp` | 1 | 8192 B (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`) | 0 | Runs `Controller::start()` (boot sequence) and returns. |
| `lighting` | `src/app/controller.cpp` | 6 | 6144 B | 1 | 20 ms tick: drain command queue, transitions/effects, batched PCA9685 flush, input polling; automation engine tick every iteration (20 ms; event → action ≤ ~35 ms measured). Watched by the task WDT. Never blocks on network. |
| `housekeeping` | `src/app/controller.cpp` | 2 | 6144 B | 0 | 250 ms: debounced state save, outbound UDP/TCP actions (`NetSender`), MQTT `poll()` (inbound commands, coalesced state/discovery publishing outside the model lock), network supervision, WS status. Watched by the task WDT (MQTT network timeout 2 s < WDT 5 s). |
| `httpd` | `src/web/web_server.cpp` (esp_http_server) | 5 | 10240 B | any | REST API, static `web.pack`, WebSocket. Handlers only validate and enqueue. |
| `ctrl` | `src/networking/control_servers.cpp` | 3 | 5120 B | 0 | `select()` loop (100 ms) for the TCP control server (≤4 clients) and the UDP discovery/control socket; pushes coalesced state lines. Created once at boot. |
| `mqtt_task` | esp-mqtt (`src/networking/mqtt_service.cpp`) | 5 (`CONFIG_MQTT_TASK_PRIORITY`) | 6144 B | any | Broker connection. Its event handler only queues inbound messages (bounded inbox of 16) — it never takes the model lock. Exists only while MQTT is enabled. |
| `espnow` | `src/networking/espnow_manager.cpp` | 3 | 6144 B | 0 | ESP-NOW: decodes/validates frames from its bounded receive queue (16), applies remote commands through the lighting enqueue path, sends HELLO (5 s) and coalesced STATE frames. Exists from boot; the radio is initialised only when enabled. |
| `restart` | `Controller::schedule_restart` | 1 | 6144 B | any | One-shot: waits, then `esp_restart()`; the shutdown handler persists pending light state and publishes MQTT `offline`. |
| ESP-IDF system tasks | `tiT` (lwIP), `sys_evt`, `wifi`, `ipc0/1`, `esp_timer`, mDNS | IDF defaults | IDF defaults | — | Platform services. |

**Lock order** (deadlock rule): model mutex (`lumen::model_mutex()`, recursive)
→ service mutex (MQTT/control/automation internals) → esp-mqtt client lock.
Code holding a service mutex never calls into esp-mqtt; the MQTT task never
takes the model lock.

Configured limits: lighting command queue 64 (overflow counted in
`stats().queue_dropped`), WS coalescing per light, MQTT inbox 16 and async
outbox 16 (overflow counted in `status.dropped`), TCP 4 clients × 1 KB line,
UDP 20 frames/s, `NetSender` bounded queue, HTTP 10 sockets with LRU purge.

## 4. Event bus (implemented in Phase 1)

`src/core/event_bus.*` — typed, bounded, publish/subscribe bus (SPEC §23 rules 4–6).

- Fixed-size `Event` (≤32 B) and `EventPayload` (≤16 B); no heap allocation.
- Each subscription owns a fixed slice of caller-provided pool storage; the bus
  never allocates on the heap.
- Producers never block: `post()` takes a short critical section only.
- Drop policy is explicit and observable via `EventBusStats`:
  * Critical : never silently dropped — evicts the OLDEST lower-priority event;
               if the queue holds only critical events the drop is counted.
  * Normal   : dropped when full → counted.
  * Telemetry: coalesces with an already-queued event of the same id;
               dropped when full → counted.
- Handlers run in the consumer's context (on `poll()`), not the producer's;
  the lock is not held during dispatch, so handlers may freely `post()`.
- Host-testable behind `ILock`/`IClock` ports (no ESP-IDF primitives).

Subscribers: lighting engine, networking, storage, diagnostics (grow per phase).

## 5. Storage and configuration (implemented in Phase 1)

`src/core/storage_manager.*` (host `PosixFs`; target `EspLittleFs`) and
`src/core/config_manager.*`:

- Transactional write: back up previous generation (.bak) → write .tmp →
  optional validate → atomic rename. Never destroys the last good copy.
- Corrupt/missing file → fall back to backup, then defaults, log, never crash.
- Config pipeline: load → parse → schema-version check → migrate → validate.
  Unknown (future) schema_version is rejected with the old file left intact.
- Secrets never stored in JSON documents (NVS only).
- Successful save publishes `config_changed` on the event bus.
- Vendored cJSON (`src/core/cJSON.{c,h}`, MIT) for JSON parsing.

## 6. System manager and diagnostics (implemented in Phase 1)

`src/core/system_manager.*` — declarative service registry (init/start/stop in
declared order, bounded startup, stop puts outputs into safe states).

`src/core/diagnostics.*` — structured log categories
(SYSTEM, HARDWARE, LIGHTING, AUTOMATION, NETWORK, MQTT, ESP_NOW, WEB, OTA,
SECURITY), secret redaction helpers, and a diagnostics snapshot API (uptime,
free heap, min free heap, reset reason) consumed later by REST.

## 7. Hardware Abstraction Layer (implemented in Phase 2)

`src/hardware/hal/` (interfaces) and `src/hardware/drivers/` (ESP-IDF
implementations, host-excluded via build filter with mocks in
`test/unit/mocks/`):

HAL interfaces (all return `esp_err_t`, no board knowledge):
- `IOutputDriver` / `IDigitalInputDriver` — digital in/out, inversion, safe state
- `IPwmDriver` — native LEDC: duty, frequency, resolution, inversion
- `II2CBus` — single owner of I2C bus(es); write/read/write-then-read/probe/scan/reset
- `IUartDriver` — RS485 / generic serial, configurable baud/format/flow control
- `IEthernetDriver` — PHY addr/MDC/MDIO/clock from board profile (never hardcoded)
- `IAddressableLedDriver` — RMT WS2812/SK6812, global brightness, color order
- `IRelayDriver` — on/off with polarity and safe state
- `IPeripheralDriver` — catch-all ioctl for sensors/expanders

P0 drivers (SPEC §7.1): `gpio_driver`, `relay_driver`, `ledc_driver`,
`i2c_manager`, `pca9685_driver`, `pcf8574_driver`. The PCA9685 driver batches
pending channel updates into minimal I2C transactions (AGENTS.md §6); lighting
channels never touch I2C directly. The PCF8574 driver debounces inputs and
generates change events.

PCA9685 batching and PCF8574 debounce/change detection are proven by host
tests against a mock I2C bus simulator with error injection.

## 8. Board profiles and PinValidator (implemented in Phase 2)

Board profiles are versioned JSON in `boards/`, validated against
`schemas/board-profile.schema.json` plus semantic PinValidator checks
(SPEC §5.3):

- GPIO existence per MCU, input-only rules, strapping pins, flash/PSRAM
  reserved pins, board-reserved pins
- Duplicate GPIO assignments, peripheral bus references resolve
- UART / I2C / Ethernet conflicts
- PWM channel / timer limits, RMT limits

MCU capability tables are per-MCU data (`include/lumen/hardware/mcu_capabilities.h`),
not `#ifdef` soup. Validation returns a structured `ValidationReport` with
stable reason codes for inline UI rendering (Phase 7). Built-in profiles:
`kincony_kc868_ap`, `generic_esp32`, `generic_esp32s3`,
`generic_esp32_pca9685`, `generic_esp32_pcf8574`, `generic_esp32_pwm`.
All KC868-AP addresses/polarities are marked `unverified` until Phase 3
measures them.

## 9. Boot order (Phase 0/1)

1. 2nd stage bootloader → partition table check (dual OTA + LittleFS).
2. `app_main`: chip/flash/heap banner via structured logs (`tag: lumen`).
3. SystemManager: register services → init_all() → start_all().
4. Heartbeat loop (Phase 0 proof of life, replaced by service tasks later).

Changing `partitions.csv` requires asking first (AGENTS.md §15); the layout
is enforced by the host test `test/test_host_partitions`.

## 10. Firmware update, rollback and backup (Phase 12)

- **Slots**: `ota_0` / `ota_1` (1 536 KB each; LittleFS `storage` 896 KB at 0x320000 — layout revised 2026-11-26). Serial flashing (`pio run -t upload`) also writes the initial
  `otadata`, so it always returns the device to `ota_0`.
- **Upload** (`src/app/ota_service.cpp`, httpd task): streamed in 4 KB chunks into the inactive slot with
  `OTA_WITH_SEQUENTIAL_WRITES` (sector erase as it goes). Pre-checks: ESP image magic, chip id, app-descriptor
  magic, project `lumen-firmware`, size ≤ slot (`src/services/ota_image.cpp`, host-tested). `esp_ota_end()`
  verifies the whole image; only then `esp_ota_set_boot_partition()`. An interrupted or corrupt upload is
  aborted and the running image stays selected.
- **Rollback** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`): the new image boots `pending_verify`. The housekeeping
  task confirms it (`esp_ota_mark_app_valid_cancel_rollback`) after ≥ 20 s with LittleFS mounted, the web server
  running and the lighting loop ticking; a reset before that (crash, watchdog, power) makes the bootloader return
  to the previous image; still unhealthy at 120 s → `esp_ota_mark_app_invalid_rollback_and_reboot()`.
  Anti-rollback (eFuse secure version), secure boot and flash encryption are **not** enabled — they are
  irreversible eFuse decisions for the owner (see Known limitations in IMPLEMENTATION_STATUS).
- **Version**: `version.txt` → `PROJECT_VER` (app descriptor) and `LUMEN_FIRMWARE_VERSION`;
  `LUMEN_VERSION=…` overrides it, `LUMEN_OTA_SELFTEST=1|2` builds rollback-drill images (scripts/pio_version.py).
- **Web UI update**: `web.pack` streamed to `web.pack.new`, validated (`WebPack::validate`), renamed over the old
  pack. When both do not fit in LittleFS the old pack is removed first; `/recovery` (compiled into the firmware)
  can always install a UI again.
- **Backup/restore** (`src/services/backup.cpp`, host-tested): streamed in both directions (RAM ≈ one document).
  Restore assembles `/lfs/config.restore`, then swaps directories (`config → config.old`, `restore → config`);
  `recover_interrupted()` at boot repairs a cut swap. After a restore, configuration writes are frozen until the
  restart (the shutdown handler does not save light state over the restored files).

## 11. Floor plans (Phase 8)

- **Storage**: overlay `config/floorplans/<id>.json` (IStorage, transactional, in backups) and the drawing
  `/lfs/floorplans/<id>.svg` (outside `config/`, so a restore's directory swap never deletes it). Max 3 plans; SVG space is
  whatever LittleFS has free (≈ 350 KB with a 480 KB UI), checked before each upload (507).
- **Firmware check** (`src/services/svg_check.cpp`): streaming character state machine (no XML parser, no full buffer):
  quote-aware tag scanner, long geometry values (`d`, `points`, …) truncated while reading, element/denylist/attribute/
  CSS/url checks, root viewBox extraction. Upload streams through it into `<id>.svg.new`, renamed only on success.
- **Web UI**: `sanitizeSvg.ts` (allowlist via DOMParser) before upload and before render; `SanitizedSvg` is the only
  `dangerouslySetInnerHTML`. `coords.ts` holds the one viewBox ↔ screen mapping: the viewer renders in the SVG's own
  viewBox, the Konva editor's stage transform is `fit()` of the same viewBox (unit-tested to land on the same pixel).
- **Viewer** (control mode, ≈ 33 KB gz): inline SVG layers (drawing, rooms, glows with one shared `feGaussianBlur`,
  interaction), Motion animates opacity/scale/gradient stops only; glow = the light's reported `output` from the live
  query cache (WebSocket-patched), never the optimistic `state`.
- **Editor** (edit mode, ≈ 83 KB gz): `react-konva` core build + 5 shapes, lazy route, never imported by the viewer
  (checked by `check_bundle_budget.mjs`). Draft/undo/redo in a Zustand store; Save → `PUT /layout`.

## 12. Multi-controller (Phase 11)

Ownership model and conflict policy (details: `docs/ESP_NOW_PROTOCOL.md`): each light has one owner; only owners
broadcast state; remote changes are commands to the owner that enter the normal enqueue path with origin
`espnow:<peer>`; commands are never forwarded, so loops are impossible. Duplicates (message id) and replays (per-sender
sequence, boot count in the high bits) are dropped; with synchronised clocks frames outside ±120 s are dropped and a
remote command older than the light's last local command is rejected (local input wins). The receive callback only
copies frames into a queue (AGENTS §4.7). ESP-NOW does not depend on MQTT or the internet.
