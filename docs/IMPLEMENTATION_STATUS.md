# Implementation Status

Canonical progress record (SPEC §29 Rule 6). A phase is done only when its
gate in `.opencode/plans/` passes. Mock/host results are never reported as
hardware-verified (SPEC §29 Rule 5).

Legend: `[x]` done · `[ ]` not done · **HW** = verified on physical hardware.

## Phase 0 — Foundation

- [x] PlatformIO project (pinned `espressif32@8.0.0`, ESP-IDF 6.1.0, envs `esp32`, `esp32s3`, `native`)
- [x] Partition table (4 MB): 2 × 1.5 MB OTA app slots + 896 KB LittleFS (revised 2026-11-25 and 2026-11-26, both owner-approved) — validated by host test
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
      LEDC dimmer + fade driven on the ESP32 DevKit (latched duty read-back, nothing attached).
- [ ] **Addressable LEDs (RMT/WS2812)**: not implemented; `rmt` lights are refused by the API (no hardware to verify).
- [x] Switch lights change instantly (a default fade only delayed switching off) — host test + HW (relay / GPIO LED)
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

- [x] Firmware: floor-plan API, overlay validation, streaming SVG safety checker (rejects, never repairs) — host suite
      with 38 malicious samples fed whole and byte-by-byte; **HW**: unsafe uploads rejected, stored drawing untouched
- [x] SVG sanitiser (allowlist) + malicious-input suite (27 tests); `SanitizedSvg` is the only `dangerouslySetInnerHTML`
- [x] Control mode (inline SVG + Motion glows from the reported output; zoom/pan/pinch; accessible list view)
- [x] Edit mode (React Konva core, lazy): place lights, room regions, drag, multi-select, align/distribute, snap,
      arrow-key nudge, undo/redo, save/discard, unsaved-change guard
- [x] Shared viewBox coordinate core, unit-tested for viewer/editor agreement; **HW** e2e: placed at (200,150) in the
      editor, stored and shown there in control mode
- [x] **HW** e2e (desktop + mobile): upload with script stripped, place + save, glow follows output changed by another
      client, tap drives the PCA9685 (read-back), axe in both modes, Konva not fetched in control mode
- [x] Budgets: viewer 33.1 KB, editor 83.5 KB (DESIGN §14.1: 40 / 90)
- [x] Glow-radius handle on the selected light's ring (one undo step per drag; slider kept for keyboard use) and
      drag-from-tray placement at the drop point (click-to-place kept for touch/keyboard) — **HW** e2e. Deviation
      from DESIGN §9.4: a purpose-built handle instead of `Konva.Transformer` (+7 KB gz, editor chunk would sit at
      its 90 KB budget)
- [ ] Sensor overlay layer (no sensors in the model yet)

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
      disable / identity change — **HW** against Home Assistant 2026.11.2
      (`test/integration/ha`, 34/34: control round trips by read-back,
      lifecycle, availability, controller/HA restart, broker outage)
- [x] TCP control server (8266, token auth, push, heartbeat) — **HW** (e2e + scripted)
- [x] UDP discovery + HMAC-signed control with replay/timestamp/rate checks — **HW**
- [x] Integrations UI (MQTT, HA, TCP/UDP, token) + Network UI — **HW** (e2e + axe)
- [x] Host tests (`test_host_integration`): HA parsing/serialisation,
      discovery, framing, HMAC RFC 4231 vector, replay guard
- [ ] Ethernet (LAN8720): code path present, opt-in, **unverified on hardware**
- [ ] ESP-NOW protocol (moved to Phase 11)

## Phase 11 — Multi-controller

- [x] ESP-NOW v1 envelope (binary header, HMAC, network tag, boot-continuous sequence) — host suite `test_host_espnow`
- [x] Peer discovery (HELLO 5 s, offline 20 s, 8 peers), state sync by owners, remote commands, network scenes
- [x] Conflict resolution: duplicates, replays, stale timestamps, local-command-wins rule; loops impossible by design
- [x] Controllers tab (Devices) + ESP-NOW card (Integrations); WS events `controller_discovered` / `controller_disconnected` / `peer_light_state`
- [x] **HW** (KC868-AP `lumen-0798` + ESP32 DevKit `lumen-e704`, `test/integration/multi_test.py` 17/17): remote
      command A→B 26 ms / B→A 31 ms median (PCA read-back), state sync 115 ms median incl. HTTP polling, network scene,
      wrong key / other network rejected, works with the MQTT broker stopped, 300-command two-way burst: 100 %
      delivered, no duplicates, no amplification, peer restart → rejoin with sequence continuity; e2e (UI) desktop + mobile
- [ ] MQTT network topics (SPEC §12.4) — not implemented
- [ ] Roles primary/secondary/gateway — not implemented (all controllers are standalone owners)
- [x] Local-wins rule — **HW** via `reliability_test.py` (receive path held 2.5 s on the test image)

## Phase 12 — Security and production

- [x] Auth hardening: sessions, CSRF, IP rate limit + account lockout — **HW** security suite (`test/integration/security_test.py`, 29 checks): 98 routes need a session, every mutating route needs CSRF, cookie flags, security headers, no secrets in any GET/diagnostics, size limits, WS refused without session and closed on logout, sessions end on logout/password change, login rate limit with Retry-After
- [x] Forced password change enforcement (**HW**)
- [x] Factory reset endpoint (erases config + credentials)
- [x] Dual-partition OTA upload with verification and automatic rollback — **HW**: valid update, corrupt /
      truncated / wrong-file / unauthenticated uploads rejected with the running firmware unchanged, crash image
      rolled back by the bootloader, unhealthy image rolled back by its health check; configuration preserved in
      every case (`test/device/maintenance_test.py`)
- [x] Web UI (`web.pack`) update endpoint + built-in `/recovery` page — **HW**
- [x] Backup / restore (full and partial, transactional, streamed, no secrets) — **HW** + host tests (hostile input, interrupted swap)
- [x] Settings → Firmware, Web interface, Backup & restore screens (unit tests; axe in e2e)
- [ ] Signed images / secure boot / flash encryption / anti-rollback: not enabled (owner decision, irreversible eFuses)
- [x] Performance measured on the device (`test/integration/perf_test.py`, on-device clock): command enqueue → hardware
      avg 13 ms / max 27 ms (target < 100 ms); automation event → action ≤ 35 ms worst case (target < 100 ms; engine now
      ticks every 20 ms); WebSocket propagation over the phone-hotspot Wi-Fi p95 165–200 ms (target < 250 ms);
      transitions ≈ 37 hardware updates/s (target ≥ 30); worst lighting tick 10 ms of 20, 0 overruns
- [x] Diagnostics complete per SPEC §21 (firmware/MCU, reset reason + boot count, task stacks, heap, lighting and
      latency stats, peripherals/I²C health incl. PCA9685 fault/recovery counts, automations, network, recent
      warnings/errors ring, ESP-NOW peers, failed heap allocations (task/size/caps) and a timestamped warning for
      each 4 KB step of the heap minimum below 24 KB, `testHooks` flag)
- [x] MQTT reconnect logging: one warning per distinct problem (esp-mqtt/esp-tls per-retry lines muted); broker may be
      given as a `.local` name (mDNS resolution) so a changing PC address does not break it
- [ ] 24 h soak (`test/integration/soak_test.py`) — not completed: stopped by the owner after 1.1 h (2026-11-26 17:05–18:14). In that window: no reboot, no new tick overruns, free heap flat at 61 KB, command latency avg 13 ms. A full 24 h run is still owed before release.
- [x] Reliability suite (`test/integration/reliability_test.py`, needs a `LUMEN_TEST_HOOKS=1` test image) — **HW**
      20/20 on 2026-11-26: PCA9685 stops answering (fault reported, loop keeps 59 ticks/s with 0 overruns,
      latest state kept), chip back → reconfigured + latest state in 0.3 s; power-cycled (sleeping) chip woken
      within 4.4 s; heap reserved down to 12 KB → no reboot, commands applied, diagnostics refused cleanly,
      failed allocations recorded; ESP-NOW peer loss → offline in 17 s, 409 for remote commands, rejoin in
      1 s; late remote command rejected as stale (local wins). Host: back-off/recovery/power-cycle +
      engine-level fault isolation tests
- [x] Task watchdog on every Lumen task: `lighting`, `housekeeping`, `espnow`, `ctrl` (reports hangs; no panic reset)
- [x] Release packaging + CI: `scripts/package_release.py` (refuses test images, version mismatch, oversized app;
      merged image verified byte-for-byte against its parts) and `.github/workflows/release.yml`;
      install/update guide `docs/FLASHING.md`

## Known limitations

- RS485, Ethernet, relay polarity on the KC868-AP: **unverified on hardware**.
- No 24 h soak yet (1.1 h run only): required before 1.0.0. Current release: `0.1.0`.
- ESP32-S3 images are compile-tested only.
- The task watchdog reports a hung task but does not reset the device (`CONFIG_ESP_TASK_WDT_PANIC` off, unchanged).
- A fresh device prints its setup-hotspot password on the serial console at boot (the provisioning path; needs physical access). This conflicts with AGENTS.md §5 "never log secrets" and is left for the owner to decide.
- Firmware uses 91 % of its 1.5 MB OTA slot (release check fails above 100 %).
- Under severe memory pressure (≈ 12 KB free) large API responses such as diagnostics are refused (500) while light commands keep working.
- Addressable LED strips: no RMT driver; the API refuses `rmt` lights with a clear error (the UI does not offer them). The installed strip is a single-colour strip on PCA9685 channel 0 (light "Ceiling").
- Wi-Fi/HTTP round trips through the phone hotspot are 60 ms median with spikes to 300 ms; on-device latency is the SPEC metric.
- Floor-plan drawings are not part of backups (overlay positions are); re-upload the SVG after restoring onto another controller.
- Moving the partition table (as on 2026-11-26) needs a serial flash; OTA cannot change it. Configuration was carried over with backup/restore.
- The automation "send TCP" action opens a short-lived connection per action; there is no persistent outbound session.
- `pio run -t uploadfs` replaces the whole LittleFS image, including `/config`. Install UI updates with Settings → Web interface (or `POST /system/ui/upload`) instead; that keeps the configuration.
- During a firmware upload the flash erase briefly stalls code running from flash; running transitions/effects can stutter for the ~30 s of the upload.
- A backup holds no secrets: after restoring onto another controller, Wi-Fi and MQTT passwords must be entered again.
- UDP control needs a synchronised clock (SNTP); frames are refused before that.
- UDP bursts faster than lwIP's receive mailbox (6 datagrams) are dropped by the network stack before the 20/s limiter; send at a steady rate.
- Home Assistant entity ids are set only when an entity is first registered; installations discovered by earlier firmware keep `light.lumen_lumen_…` ids until the device is removed in HA or the entities are renamed.

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
| 2026-11-25 | same | Integration suite vs. Mosquitto 2.1.4 + Home Assistant 2026.11.2 (`test/integration/ha/ha_test.py`): MQTT, HA, HTTP, TCP, UDP | PASS — 34/34 (twice) |
| 2026-11-25 | same | OTA drills: valid 0.1.1 (ota_0→ota_1, confirmed after 20 s), corrupt/truncated/wrong/unauthenticated rejected, crash image → bootloader rollback (aborted), unhealthy image → health-check rollback (invalid); backup/restore full + partial; UI pack install; config preserved | PASS |
| 2026-11-26 | same | Repartition (1.5 MB slots, 896 KB LittleFS) by serial flash; configuration restored from backup; NVS secrets kept | PASS |
| 2026-11-26 | same | Floor plans: API + unsafe-SVG rejection; e2e desktop + mobile (upload, place, save, glow follows output, tap → PCA read-back, axe) | PASS |
| 2026-11-26 | same | Security suite 29/29; HA/MQTT/HTTP/TCP/UDP suite 34/34; e2e 14/14 | PASS |
| 2026-11-26 | same | Performance (perf_test.py): all on-device targets met; reset reason / boot counter / log ring live in diagnostics | PASS |
| 2026-11-26 | KC868-AP + ESP32 DevKit (ESP32-D0WD-V3, MAC a0:b7:65:62:e7:04, CP2102 /dev/ttyUSB1) | ESP-NOW multi-controller suite 17/17; e2e 16/16 incl. Controllers tab | PASS |
| 2026-11-26 | KC868-AP | Single-colour LED strip on PCA9685 ch0: off/on/dim/fade/breathing sequence, read-back matches | PASS — visually confirmed by the owner |
| 2026-11-26 | KC868-AP | Floor-plan editor e2e: drag-from-tray placement at the drop point, glow-radius handle (desktop); full e2e 17/17 | PASS |
| 2026-11-26 | KC868-AP (+ DevKit) | Reliability suite with a `LUMEN_TEST_HOOKS=1` image: I²C fault + recovery, power-cycled PCA9685, 12 KB heap, ESP-NOW peer loss, local-wins | PASS — 20/20 |
| 2026-11-26 | KC868-AP | MQTT broker stopped/started: disconnect detected at once, reconnected 3 s after restart, publishing resumed | PASS |
| 2026-11-26 | ESP32 DevKit | LEDC dimmer on `ledc_pwm0` (GPIO15, nothing attached): 100/50/10/0 % → 100/18.4/1.1/0 % duty, 1.5 s fade mid-way at 0.75 s (driver-latched read-back) | PASS (no physical measurement) |
| 2026-11-26 | KC868-AP + DevKit | Release-configuration image (watchdogs, PCA recovery): multi-controller 17/17, HA/MQTT/TCP/UDP 34/34, perf 4/4, test routes absent (404) | PASS |
