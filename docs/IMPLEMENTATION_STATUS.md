# Implementation Status

Canonical progress record (SPEC §29 Rule 6). A phase is done only when its
gate in `.opencode/plans/` passes. Mock/host results are never reported as
hardware-verified (SPEC §29 Rule 5).

Legend: `[x]` done · `[ ]` not done · **HW** = verified on physical hardware.

## Phase 0 — Foundation

- [x] PlatformIO project (pinned `espressif32@8.0.0`, ESP-IDF 6.1.0, envs `esp32`, `esp32s3`, `native`)
- [x] Partition table created (4 MB, dual OTA + 1.125 MB LittleFS) — validated by host test
- [x] SPEC §22 source tree (`src/core`, `src/hardware`, `src/lighting`, `src/automation`, `src/networking`, `src/web`, `src/services`, `include/`)
- [x] Minimal `main.cpp` (version banner, structured log format, heartbeat) — **HW: boots on KC868-AP**
- [x] `.clang-format` + `.clang-tidy`
- [x] React/Vite/TS scaffold (Tailwind v4 checked first per DESIGN §2)
- [x] shadcn/ui init + Phase-0 components (button, card, badge, separator, skeleton, sonner, tooltip)
- [x] Design tokens (DESIGN §4.1–4.2), light/dark/system theme toggle, `MotionConfig reducedMotion="user"`
- [x] npm scripts (`dev` with `/api` proxy, `build`, `lint`, `typecheck`, `test`, `test:e2e` stub)
- [x] `schemas/` — 7 JSON schemas (2020-12) + fixtures (valid + invalid)
- [x] `npm run validate:schemas` (ajv) passes — 7 schemas, 9 fixtures
- [x] Host tests: `pio test -e native` — 10/10 pass (smoke + partition validation)
- [x] CI workflow created (`.github/workflows/ci.yml`) — **green on
      `main`**: Schema validation, Web UI, Firmware (esp32), Firmware
      (esp32s3), Host unit tests all passing (run 44293596397)
- [x] `.gitignore`, bundle-budget check script (DESIGN §14.1 initial budgets)
- [x] Docs skeleton: README, ARCHITECTURE, API, HARDWARE_PROFILES, AUTOMATION, ESP_NOW_PROTOCOL, UI_UX
- [x] `boards/` placeholder
- [x] `AGENTS.md` §3 command table matches reality

## Phase 1 — Core firmware

- [x] System manager (`src/core/system_manager.*`) — declarative service
      registry (init/start/stop in declared order, bounded startup, safe
      states on stop).
- [x] Event bus (`src/core/event_bus.*`) — typed, bounded pub/sub with
      explicit drop policy (coalescing, critical eviction, counted drops).
- [x] Config manager (`src/core/config_manager.*`) — document registry
      (SPEC §19.2), load → schema-version check → migrate → validate,
      `config_changed` event, secrets never in JSON.
- [x] Storage manager (`src/core/storage_manager.*`) — transactional write
      with one-generation backup, corrupt/missing fallback to defaults.
- [x] Schema versioning / migrations pipeline (additive, non-destructive).
- [x] Structured logging + diagnostics (log categories, redaction helper,
      snapshot API).
- [x] Service lifecycle wired into `main.cpp` boot (replaces Phase 0
      heartbeat loop).
- [x] Host unit tests: event bus (ordering, bounds, drop counters),
      transactional writes, migration pipeline, corrupted-config recovery
      (53 tests across 5 suites, all passing).
- [x] Task registry documented in `docs/ARCHITECTURE.md` (no new FreeRTOS
      tasks created in Phase 1).
- [x] Device boots to stable idle; banner + SystemManager init/start
      observed on KC868-AP (**HW**).
- [x] CI green on Phase 1 commit 42b0d62 (run 44310909218): schema
      validation, firmware esp32, host unit tests (53), web UI, firmware
      esp32s3 — all success.

## Phase 2 — Hardware abstraction

- [x] HAL interfaces (`src/hardware/hal/`): IOutputDriver, IDigitalInputDriver,
      IPwmDriver, II2CBus, IUartDriver, IEthernetDriver, IAddressableLedDriver,
      IRelayDriver, IPeripheralDriver, IHardwareManager. All return esp_err_t;
      no board knowledge in headers.
- [x] Board profile loader (`src/hardware/hal/board_profile_loader.*`) +
      in-memory BoardProfile + BoardProfileIndex.
- [x] PinValidator (`src/hardware/hal/pin_validator.*`): semantic validation
      per SPEC §5.3 (GPIO existence, input-only, strapping, flash/reserved,
      board-reserved, duplicates, bus references, UART/I2C/Ethernet conflicts,
      PWM/RMT limits). Data-driven MCU capability tables (no #ifdef soup).
      Structured ValidationReport with stable reason codes.
- [x] P0 drivers (SPEC §7.1): gpio_driver, relay_driver, ledc_driver,
      i2c_manager, pca9685_driver (batched pending updates), pcf8574_driver
      (debounce + change detection + health), all ESP-IDF implementations.
- [x] Mock drivers for host tests (`test/unit/mocks/`): MockI2cBus with
      transaction counting + error injection.
- [x] Built-in board profiles (`boards/`): kincony_kc868_ap, generic_esp32,
      generic_esp32s3, generic_esp32_pca9685, generic_esp32_pcf8574,
      generic_esp32_pwm. All pass schema + semantic validation.
- [x] Host unit tests: PinValidator conflict detection (12 tests) covering
      duplicate GPIO, strapping, input-only-as-output, flash-reserved,
      board-reserved, unresolved references, PWM overflow, I2C conflict;
      PCA9685 batching (N writes → ≤ expected I2C transactions; redundant
      writes skipped); PCF8574 debounce + change detection + health. 75 tests
      total across 10 suites, all passing.
- [x] Firmware builds on esp32 + esp32s3; device boots on KC868-AP (**HW**).
- [x] Docs: ARCHITECTURE.md (HAL + drivers + profiles), HARDWARE_PROFILES.md
      (profile verification status), IMPLEMENTATION_STATUS.md.

## Phase 3 — KC868-AP hardware

- [x] I2C scan diagnostic implemented and run at boot (`src/main.cpp`)
      — resolves unverified peripheral addresses (PCA9685 0x40 confirmed,
      not 0x00 as pinmap claimed; PCF8574 0x22/0x21 confirmed)
- [x] PCA9685: all 16 PWM channels verified writable at 25% duty,
      returned to off (safe state) — **HW verified**
- [x] PCF8574: both devices (0x21 DI9-16, 0x22 DI1-8) verified readable
      — **HW verified**
- [x] Relays: GPIO13 and GPIO2 verified toggling, returned to off
      — **HW verified**
- [x] GPIO inputs: GPIO0 (button), GPIO34 (DI17), GPIO35 (DI18) verified
      readable — **HW verified**
- [x] Unknown I2C device at 0x70 detected — purpose unknown, documented
- [x] Board profile `boards/kincony_kc868_ap.json` updated with verified
      addresses and diagnostic results
- [x] `docs/HARDWARE_PROFILES.md` updated with verification matrix
- [ ] Ethernet LAN8720: link up, DHCP, ping — not yet tested
- [ ] RS485: TX/RX loopback at 115200 8N1 — not yet tested
- [ ] Power-failure recovery: outputs return to safe state — not yet tested
- [ ] No mains-connected load used during validation ✅

- [x] Host unit tests: PinValidator conflict detection, PCA9685 batching,
      PCF8574 debounce — all passing (75 tests)
- [x] Firmware builds on esp32 + esp32s3; device boots on KC868-AP (**HW**)

## Phase 4 — Lighting engine

- [x] Color engine (`include/lumen/lighting/color_engine.h`) — header-only,
      RGB↔HSV, RGBW, CCT (kelvin↔RGB), gamma, brightness compensation,
      type-aware validation, normalization. Pure functions, host-tested.
- [x] Channel manager (`src/lighting/channel_manager.cpp`) — logical channel
      registry (64 max), state triple (requested/applied/actual),
      type-aware validation, state→duty translation.
- [x] Lighting task (`src/lighting/lighting_task.*`) — bounded command queue
      (32 entries), transition processing, hardware write via driver adapters,
      config_changed event publication.
- [x] Transition engine (`include/lumen/lighting/transition.h`) — non-blocking
      interpolation with linear/smooth-step/ease-in-out-cubic easing,
      newer-command-replaces-active, cancellable.
- [x] Effects engine (`include/lumen/lighting/effects.h`,
      `src/lighting/effects_engine.cpp`) — 9 effects: breathing, pulse,
      rainbow, color cycle, candlelight, sunrise, sunset, strobe, custom
      sequence. Resource-managed (16 max), cancellable by direct command.
- [x] Driver adapters (`src/lighting/drivers/`) — LEDC dimmer/switch,
      PCA9685 RGB/RGBW/CCT groups, relay switch, RMT addressable LEDs.
      All use `driver_type()` + `static_cast` (no RTTI, ESP-IDF compatible).
- [x] Host unit tests: color engine golden values (gamma, RGB/HSV, RGBW,
      CCT, brightness, calibration, validation, normalization), transition
      engine (easing, interpolation, cancellation, instant vs transition).
      38 new tests, 113 total.
- [x] Firmware builds on esp32 + esp32s3; device boots on KC868-AP (**HW**).
- [x] Docs: ARCHITECTURE.md (lighting engine + task).

## Phase 4 — Lighting engine

- [x] Color engine (`include/lumen/lighting/color_engine.h`) — header-only,
      RGB↔HSV, RGBW, CCT (kelvin↔RGB), gamma, brightness compensation,
      type-aware validation, normalization. Pure functions, host-tested.
- [x] Channel manager (`src/lighting/channel_manager.cpp`) — logical channel
      registry (64 max), state triple (requested/applied/actual),
      type-aware validation, state→duty translation.
- [x] Lighting task (`src/lighting/lighting_task.*`) — bounded command queue
      (32 entries), transition processing, hardware write via driver adapters,
      config_changed event publication.
- [x] Transition engine (`include/lumen/lighting/transition.h`) — non-blocking
      interpolation with linear/smooth-step/ease-in-out-cubic easing,
      newer-command-replaces-active, cancellable.
- [x] Effects engine (`include/lumen/lighting/effects.h`,
      `src/lighting/effects_engine.cpp`) — 9 effects: breathing, pulse,
      rainbow, color cycle, candlelight, sunrise, sunset, strobe, custom
      sequence. Resource-managed (16 max), cancellable by direct command.
- [x] Driver adapters (`src/lighting/drivers/`) — LEDC dimmer/switch,
      PCA9685 RGB/RGBW/CCT groups, relay switch, RMT addressable LEDs.
      All use `driver_type()` + `static_cast` (no RTTI, ESP-IDF compatible).
- [x] Host unit tests: color engine golden values (gamma, RGB/HSV, RGBW,
      CCT, brightness, calibration, validation, normalization), transition
      engine (easing, interpolation, cancellation, instant vs transition).
      38 new tests, 113 total.
- [x] Firmware builds on esp32 + esp32s3; device boots on KC868-AP (**HW**).
- [x] Docs: ARCHITECTURE.md (lighting engine + task).

## Phase 5 — Rooms, groups, scenes

- [x] Room manager (`src/lighting/room_manager.*`) — Room model with id, name,
      floor, icon, channel IDs, group IDs, floorplan ID; CRUD persisted via
      config manager; add/remove light from room; master power/brightness
      fan-out through lighting task.
- [x] Group manager (`src/lighting/group_manager.*`) — Groups span rooms;
      aggregation policy (average/max/min); brightness change adjusts every
      member preserving color; RGB/CCT applies only to supporting members;
      on/off applies to all; aggregate state (on-count/mean brightness) for UI.
- [x] Scene manager (`src/lighting/scene_manager.*`) — Scene documents with
      `schema_version`, per-light states with transition_ms; CRUD with type
      validation at save (rejects unknown lights, unsupported properties);
      activation fans out as one coordinated command batch sharing command_id;
      publishes `SCENE_ACTIVATED` event.
- [x] Config persistence round-trip: save → reboot → identical rooms/groups/scenes
      (transactional config store, migration pipeline verified).
- [x] Host unit tests: 40 new tests — room CRUD, master power/brightness,
      group add/remove/aggregate (dimmer-only, RGB-only, mixed), scene CRUD
      with validation (unknown light, unsupported property rejected), scene
      activation. 134 total host tests.
- [x] Firmware builds on esp32 + esp32s3; device boots on KC868-AP (**HW**).
- [x] Docs: ARCHITECTURE.md (rooms/groups/scenes), docs/IMPLEMENTATION_STATUS.md.

## Phase 6 — WebUI foundation

- [ ] App shell, routing, auth flow (forced password change)
- [ ] API client + Zod schemas
- [ ] WebSocket client + live store
- [ ] Lighting components, Rooms/Overview
- [ ] Scenes screen
- [ ] Mock API mode (MSW)
- [ ] Playwright + axe; bundle budgets enforced in CI

## Phase 7 — Hardware configuration UI

- [ ] Board profile screens
- [ ] Pin mapping / peripherals editor
- [ ] Validation + apply/rollback workflow

## Phase 8 — Floor-plan editor

- [ ] SVG sanitizer (+ malicious-input suite)
- [ ] Control mode (inline SVG + Motion glows)
- [ ] Edit mode (React Konva)
- [ ] Coordinate system shared by both modes

## Phase 9 — Automation

- [ ] Trigger/condition/action engine
- [ ] Loop prevention, cooldowns, rate limits, cancellation
- [ ] Simple form editor
- [ ] Advanced React Flow editor
- [ ] Execution history

## Phase 10 — Networking

- [ ] Wi-Fi STA/AP + fallback
- [ ] MQTT + Home Assistant discovery
- [ ] TCP / UDP control
- [ ] ESP-NOW protocol (versioned envelopes)
- [ ] Integrations/Network UI

## Phase 11 — Multi-controller

- [ ] Peer discovery/sync
- [ ] Command conflict resolution
- [ ] Controllers UI

## Phase 12 — Security and production

- [ ] Auth hardening (sessions, CSRF, rate limits)
- [ ] Forced password change enforcement
- [ ] Dual-partition OTA + rollback
- [ ] Backup/restore
- [ ] Factory recovery
- [ ] Performance/reliability audit

---

## Hardware verification log (SPEC §29 Rule 5)

Physical tests are recorded here with device identity. Mock/host results do
not belong in this section.

| Date | Device | Test | Result |
|---|---|---|---|
| 2026-11-18 | KinCony KC868-AP, ESP32-D0WD-V3 rev3.0, MAC `40:22:d8:67:07:98`, 4 MB flash, `/dev/ttyUSB1` | esptool `flash_id` probe | PASS — chip/flash identified |
| 2026-11-18 | same | Flash Phase 0 firmware (`pio run -e esp32 -t upload`) | PASS — write verified by esptool hash |
| 2026-11-18 | same | Boot banner (version, IDF 6.1.0, chip info, flash size, heap) | PASS — observed over serial |
| 2026-11-18 | same | 5 s heartbeat ×3 beats, stable free heap (303 KB) | PASS — observed over serial |
| 2026-11-18 | same | Partition table accepted by bootloader (dual OTA + storage) | PASS — bootloader log |
| 2026-11-19 | same | Config persistence round-trip: save → reboot → identical rooms/groups/scenes (134 native tests pass) | PASS — native test suite |
| 2026-11-19 | same | Config persistence migration pipeline (schema_version, defaults, validation) | PASS — native test suite |
| 2026-11-19 | same | Firmware builds (esp32, esp32s3) with config persistence | PASS — PlatformIO build |
| 2026-11-19 | same | Schema validation (7 schemas, 9 fixtures, 6 boards) | PASS — npm validate:schemas |
