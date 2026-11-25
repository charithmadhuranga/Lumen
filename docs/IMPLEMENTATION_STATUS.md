# Implementation Status

Canonical progress record (SPEC §29 Rule 6). A phase is done only when its
gate in `.opencode/plans/` passes. Mock/host results are never reported as
hardware-verified (SPEC §29 Rule 5).

Legend: `[x]` done · `[ ]` not done · **HW** = verified on physical hardware.

## Phase 0 — Foundation

- [x] PlatformIO project (pinned `espressif32@8.0.0`, ESP-IDF 6.1.0, envs `esp32`, `esp32s3`, `native`)
- [x] Partition table (4 MB): 2 × 1.625 MB OTA app slots + 640 KB LittleFS (grown from 1.25 MB slots on 2026-11-25, approved) — validated by host test
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

> **Correction (2026-11-25).** Earlier entries for Phases 3–5 were written
> while the code could not work: the managers discarded commands, the PCA9685
> encoding never turned an output on, the PCF8574 latch was never released,
> every `/{id}` route was broken and the KC868-AP profile had a GPIO16
> conflict. Those defects are fixed (commit d85b0c0) and the items below are
> re-verified on the device by PCA9685 register read-back, not by "code
> exists". Phase 3's PCA9685 "verified writable" claim is only true from
> d85b0c0 on.

## Phase 4 — Lighting engine

- [x] Colour engine, channel manager (requested / applied state, 64 lights),
      perceptual (CIE) dimming with 16-bit duties, CCT warm/cool mix.
- [x] Lighting task: bounded queue (64, own mutex), non-blocking transitions,
      newer command replaces transition/effect, redundant writes skipped,
      batched PCA9685 flush; listener events (requested / settled / config).
- [x] Effects (breathing, pulse, rainbow, colour cycle, candlelight, sunrise,
      sunset, strobe); duration 0 runs until replaced.
- [x] Driver adapters (`src/lighting/drivers/driver_adapter.cpp`): PCA9685
      dimmer/RGB/RGBW/CCT with channel offsets, LEDC, relay/GPIO switch.
- [ ] **Addressable LEDs (RMT/WS2812)**: adapter exists, but no ESP-IDF RMT
      driver is built by the hardware manager — such lights report a
      hardware error. Not implemented.
- [x] Host tests (`test_host_engine`, 14): command → hardware duties, offsets,
      transitions, effects, CCT, removal re-indexing.
- [x] **HW**: dimmer/RGB/CCT/relay lights driven and verified by PCA9685
      read-back; 2 s fade observed; state persists across reboot.

## Phase 5 — Rooms, groups, scenes

- [x] Room/group/scene managers enqueue real commands (were no-ops), cJSON
      loaders, transactional persistence; deleting a light detaches it everywhere.
- [x] Scene activation, capture, duplicate; group command/power/brightness.
- [x] Host tests (`test_host_lighting`, fan-out test in `test_host_engine`).
- [x] **HW**: room control from the UI verified by read-back (e2e). Scene/group fan-out is host-tested only.

## Phase 6 — WebUI foundation

- [x] App shell, routing, auth flow with forced password change
- [x] API client (single envelope unwrap) + Zod schemas; TanStack Query / Zustand split
- [x] WebSocket client + live store (session-authenticated upgrade)
- [x] Overview, Rooms, Scenes, Devices screens
- [x] Vitest + RTL unit tests (36)
- [x] Playwright + axe against the real device (`LUMEN_URL`, `LUMEN_PASSWORD`): 12/12, desktop + mobile, light + dark
- [x] Bundle budgets enforced (`npm run check:budget`): initial 158.1/160 KB gz, CSS 12.8/25 KB, total 353/450 KB
- [ ] MSW mock-API mode — not implemented (tests stub `fetch`)

## Phase 7 — Hardware configuration UI

- [x] Board profile list/detail, validation and apply with rollback
- [x] Pin map, outputs (test pulse, read-back), inputs, I²C scan
- [x] **HW**: output read-back and I²C scan on the KC868-AP. Profile apply/rollback with a deliberately failing profile: not yet exercised on hardware.

## Phase 8 — Floor-plan editor

- [ ] SVG sanitizer (+ malicious-input suite)
- [ ] Control mode (inline SVG + Motion glows)
- [ ] Edit mode (React Konva)
- [ ] Coordinate system shared by both modes
- Screen shows "not available in this firmware build" — nothing is faked.

## Phase 9 — Automation

- [x] Trigger/condition/action engine (time, sun, input, light, MQTT, variable triggers)
- [x] Loop prevention (chain depth), cooldowns, rate limits, cancellation
- [x] Simple form editor and React Flow editor (lazy-loaded)
- [x] Execution history; per-automation files (`automations/<id>.json`)
- [x] Host tests (`test_host_automation`, 13); **HW**: delay, test run, disable, loop detection, UI-built automation (e2e)

## Phase 10 — Networking

- [x] Wi-Fi STA + WPA2 fallback hotspot, mDNS `<hostname>.local`, SNTP — **HW**
- [x] MQTT (`src/networking/mqtt_service.cpp`): set/state/group/scene topics,
      retained state, LWT + clean `offline` on restart, TLS with the cert
      bundle, password in NVS, bounded inbox/outbox, direct publishing outside
      the model lock — **HW** against a local Mosquitto 2 broker
- [x] Home Assistant discovery (lights + diagnostics), cleanup on delete /
      disable / identity change — **HW** (retained topics inspected); not yet
      tested against a running Home Assistant instance
- [x] TCP control server (8266, token auth, push, heartbeat) — **HW** (e2e + scripted)
- [x] UDP discovery + HMAC-signed control with replay/timestamp/rate checks — **HW**
- [x] Integrations UI (MQTT, HA, TCP/UDP, token) + Network UI — **HW** (e2e + axe)
- [x] Host tests (`test_host_integration`): HA parsing/serialisation,
      discovery, framing, HMAC RFC 4231 vector, replay guard
- [ ] Ethernet (LAN8720): code path present, opt-in, **unverified on hardware**
- [ ] ESP-NOW protocol (moved to Phase 11)

## Phase 11 — Multi-controller

- [ ] ESP-NOW versioned envelopes
- [ ] Peer discovery/sync
- [ ] Command conflict resolution
- [ ] Controllers UI

## Phase 12 — Security and production

- [x] Auth hardening: sessions, CSRF, IP rate limit + account lockout (**HW**)
- [x] Forced password change enforcement (**HW**)
- [x] Factory reset endpoint (erases config + credentials)
- [ ] Dual-partition OTA upload + rollback; web UI (`web.pack`) update endpoint
- [ ] Backup/restore (Settings shows these as unavailable)
- [ ] Long soak test (heap ≈ 80 KB free after boot with MQTT — watch)
- [ ] Performance/reliability audit

## Known limitations

- RS485, Ethernet, relay polarity on the KC868-AP: **unverified on hardware**.
- Addressable LED strips: no RMT driver yet (see Phase 4).
- The automation "send TCP" action opens a short-lived connection per action; there is no persistent outbound session.
- `pio run -t uploadfs` replaces the whole LittleFS image, including `/config`; until the Phase 12 update endpoint exists, re-create configuration afterwards (`test/device/setup_demo.py` for the demo set). NVS secrets (passwords, MQTT password, integration token) survive.
- UDP control needs a synchronised clock (SNTP); frames are refused before that.

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
| 2026-11-25 | same, `/dev/ttyUSB0` | PCA9685 at 0x40, PCF8574 at 0x22/0x21 detected by I²C scan; outputs driven by lights verified by PCA register read-back | PASS |
| 2026-11-25 | same | Wi-Fi STA, mDNS, SNTP; auth + forced password change; WebSocket upgrade with session | PASS |
| 2026-11-25 | same | Automations: delay, test, disable, loop detection | PASS |
| 2026-11-25 | same | Playwright e2e on the device: 12/12 (control, automation, TCP integration, axe light/dark, desktop + mobile) | PASS |
| 2026-11-25 | same | MQTT vs. Mosquitto 2 (auth): commands → read-back, retained state, invalid payload rejected, HA discovery publish + cleanup, LWT/offline on disable and restart | PASS |
| 2026-11-25 | same | TCP 8266 (auth, commands, push, errors) and UDP 8267 (discovery, HMAC control; replay, bad MAC, stale ts rejected; rate limit) | PASS |
