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
| `lighting` | `src/app/controller.cpp` | 6 | 6144 B | 1 | 20 ms tick: drain command queue, transitions/effects, batched PCA9685 flush, input polling; automation tick every 3rd iteration (60 ms). Watched by the task WDT. Never blocks on network. |
| `housekeeping` | `src/app/controller.cpp` | 2 | 6144 B | 0 | 250 ms: debounced state save, outbound UDP/TCP actions (`NetSender`), MQTT `poll()` (inbound commands, coalesced state/discovery publishing outside the model lock), network supervision, WS status. Watched by the task WDT (MQTT network timeout 2 s < WDT 5 s). |
| `httpd` | `src/web/web_server.cpp` (esp_http_server) | 5 | 10240 B | any | REST API, static `web.pack`, WebSocket. Handlers only validate and enqueue. |
| `ctrl` | `src/networking/control_servers.cpp` | 3 | 5120 B | 0 | `select()` loop (100 ms) for the TCP control server (≤4 clients) and the UDP discovery/control socket; pushes coalesced state lines. Created once at boot. |
| `mqtt_task` | esp-mqtt (`src/networking/mqtt_service.cpp`) | 5 (`CONFIG_MQTT_TASK_PRIORITY`) | 6144 B | any | Broker connection. Its event handler only queues inbound messages (bounded inbox of 16) — it never takes the model lock. Exists only while MQTT is enabled. |
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
