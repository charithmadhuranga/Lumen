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

- [ ] System manager
- [ ] Event bus
- [ ] Config manager
- [ ] Storage manager (NVS + LittleFS, transactional)
- [ ] Schema versioning / migrations
- [ ] Structured logging + diagnostics
- [ ] Service lifecycle + task registry

## Phase 2 — Hardware abstraction

- [ ] HAL interfaces
- [ ] Board profiles
- [ ] Pin validator
- [ ] GPIO / LEDC / I2C drivers
- [ ] Transactional profile apply

## Phase 3 — KC868-AP hardware

- [ ] KC868-AP reference profile
- [ ] PCA9685 / PCF8574 / relay drivers
- [ ] On-device bring-up tests (**HW**)
- [ ] Physical verification of pins, polarity, I2C address (scan first — address `0x00` unverified)

## Phase 4 — Lighting engine

- [ ] Logical channels + normalized LightState
- [ ] Color engine (RGB/HSV/RGBW/CCT/gamma)
- [ ] Non-blocking transitions
- [ ] Effects engine

## Phase 5 — Rooms, groups, scenes

- [ ] Rooms
- [ ] Groups + aggregation policies
- [ ] Scenes

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
