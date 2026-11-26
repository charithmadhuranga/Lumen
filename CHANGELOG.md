# Changelog

All notable changes to Lumen. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The firmware version lives in
`version.txt`. Release tags are `v<version>`, and publishing a GitHub release
for a tag builds and attaches the firmware (`.github/workflows/release.yml`).
Installation and update steps are in [docs/FLASHING.md](docs/FLASHING.md).

## [Unreleased]

## [1.0.0-rc.1] - 2026-11-26

First release candidate. Every implementation phase (0–12) is complete and
tested on the KinCony KC868-AP reference board. One item from the release
gate is still open: the 24-hour soak test (see "Known limitations").

### Firmware

- **Platform**: ESP-IDF 6.1.0 (PlatformIO `espressif32@8.0.0`), C++17,
  FreeRTOS. The 4 MB layout has two 1.5 MB OTA slots and an 896 KB LittleFS.
  Builds for ESP32 and ESP32-S3.
- **Hardware abstraction**: board profiles as versioned JSON, a pin
  validator (conflicts, strapping and input-only pins, I²C address clashes),
  drivers for GPIO, relays, LEDC PWM, PCA9685 (batched I²C writes) and
  PCF8574 inputs. Built-in profiles: KinCony KC868-AP, generic ESP32 and
  ESP32-S3 variants.
- **Lighting engine**: switch, dimmer, RGB, RGBW and CCT lights, with
  requested, applied and actual state kept separate. Transitions never
  block, nine effects are built in, a newer command replaces a running
  transition, redundant writes are skipped and switches change instantly.
  Command latency on the device is 13 ms on average and 28 ms at worst
  (enqueue to hardware).
- **Rooms, groups, scenes**: scenes can be captured, activated and
  duplicated; group control works across mixed light types.
- **Automations**: triggers, conditions and actions, all run locally. Time
  and sunrise/sunset scheduling, inputs, light, MQTT and variable triggers;
  loop prevention, cooldowns, rate limits, cancellation and execution
  history.
- **Networking**: Wi-Fi station with a WPA2 fallback hotspot, mDNS, SNTP;
  MQTT with Home Assistant discovery; a TCP control server with token
  authentication; UDP discovery and HMAC-signed UDP control.
- **Multiple controllers**: ESP-NOW v1 (HMAC-authenticated frames, peer
  discovery, state sync from the owning controller, remote commands,
  network-wide scenes, replay and stale protection, "local command wins").
- **Floor plans**: a firmware SVG safety checker that rejects unsafe input
  and never repairs it. Up to 3 plans of 128 KB each, with overlay data kept
  separate from the drawing.
- **Security**: salted PBKDF2-HMAC-SHA256 passwords, a forced password
  change for the `admin`/`admin` default, sessions with CSRF protection, IP
  rate limiting and account lockout, secrets kept in NVS only (never in JSON,
  backups or logs), and no unauthenticated management hotspot.
- **Maintenance**: OTA updates with image verification and automatic
  rollback (bootloader rollback plus a health check), web UI updates, a
  `/recovery` page compiled into the firmware, and streamed transactional
  backup/restore (full or partial).
- **Diagnostics**: firmware and MCU details, reset reason and boot count,
  task stacks, heap (with failed-allocation records and low-memory
  warnings), lighting latency and overruns, peripheral health, ESP-NOW
  peers, and recent warnings and errors.
- **Reliability**: if a PCA9685 stops answering, the driver backs off and
  then recovers, and a chip that power-cycled is detected and woken. The
  task watchdog covers the `lighting`, `housekeeping`, `espnow` and `ctrl`
  tasks.

### Web UI

- React 19, TypeScript, Vite, Tailwind CSS, shadcn/ui, TanStack Query,
  Zustand and Zod. It works offline (no CDN, no external requests) and is
  served gzipped from the controller in 484 KB of the 500 KB budget.
- Screens: Overview, Rooms, Scenes, Floor plans, Automations, Devices
  (including other controllers), Hardware (profiles, pin map, outputs with
  read-back, inputs, I²C scan, diagnostics), Integrations (MQTT, Home
  Assistant, TCP/UDP, ESP-NOW), Network, Settings (firmware, web interface,
  backup and restore).
- The floor-plan viewer uses inline SVG with Motion glows that follow each
  light's actual output. The editor (React Konva, loaded only on demand)
  supports placing lights by dragging them from the tray or clicking, a
  glow-radius handle, room regions, multi-select, align and distribute,
  snap, zoom and pan, and undo/redo.
- An automation editor in two forms: a simple form and a visual editor
  built on React Flow.
- Light and dark themes, respects `prefers-reduced-motion`, and no serious
  or critical axe violations on the main routes.

### Tests

- 197 host unit tests (`pio test -e native`). The web UI has unit tests with
  Vitest/RTL and 17 Playwright + axe end-to-end tests against the real
  controller.
- Suites run against the real devices: Home Assistant / MQTT / TCP / UDP
  (34), security (29), performance, multi-controller ESP-NOW (17),
  OTA/backup drills, and reliability with fault injection (20, using a test
  image built with `LUMEN_TEST_HOOKS=1`).

### Known limitations

- **24-hour soak not completed.** A 1.1-hour run showed no reboot, flat
  heap and no new overruns. A full run is required before 1.0.0.
- **Not verified on hardware**: KC868-AP Ethernet (LAN8720), RS485, relay
  polarity and power-failure recovery. The ESP32-S3 images are only
  compile-tested.
- No driver for addressable LED strips (WS2812/RMT); such lights are refused
  with a clear error.
- Not implemented: MQTT network topics between controllers (SPEC §12.4) and
  primary/secondary/gateway controller roles. Every controller owns its own
  lights.
- Secure boot, flash encryption and signed images are not enabled. Enabling
  them burns eFuses irreversibly, so it is left to the owner.
- A fresh device prints its setup-hotspot password on the serial console at
  boot. This is how a new board is provisioned (see FLASHING.md), and
  physical access is required to read it.
- A backup holds no secrets: Wi-Fi and MQTT passwords must be entered again
  after restoring onto another controller. Floor-plan drawings are not part
  of backups.
- The firmware uses 91 % of its 1.5 MB OTA slot.

[Unreleased]: https://github.com/charithmadhuranga/Lumen/compare/v1.0.0-rc.1...HEAD
[1.0.0-rc.1]: https://github.com/charithmadhuranga/Lumen/releases/tag/v1.0.0-rc.1
