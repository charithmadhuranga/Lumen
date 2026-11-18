# Release Gate — Definition of Done (SPEC §30)

**Skill:** `lumen-testing`

Run this gate after Phase 12. Every box must be checked with **evidence**
(test result, hardware run record, or measurement) before calling the first
production-ready release done. A box backed only by code that was never run
does not count (SPEC §29 Rule 7).

## Hardware

- [ ] KC868-AP preset — profile verified against physical board, values in
      `docs/HARDWARE_PROFILES.md` with dates.
- [ ] Generic ESP32 preset.
- [ ] Generic ESP32-S3 preset.
- [ ] Custom board editor (create/duplicate/import/export/validate/apply).
- [ ] Pin conflict detection (dedicated test suite green).
- [ ] PCA9685 — real I2C communication proven (address recorded).
- [ ] PCF8574 — both devices, addresses and polarity recorded.
- [ ] Native PWM.
- [ ] Relay — polarity and boot state recorded.
- [ ] RMT addressable LEDs — real strip test.
- [ ] Ethernet — link + traffic recorded (or listed as known failure).
- [ ] RS485 — loopback/terminal test recorded.

## Lighting

- [ ] Single-channel dimming · [ ] Relay lighting · [ ] RGB · [ ] RGBW ·
      [ ] CCT · [ ] Addressable LEDs
- [ ] Effects (all 9) · [ ] Transitions (non-blocking, verified by review +
      timing test)
- [ ] Rooms · [ ] Groups (mixed-type aggregation tested) · [ ] Scenes
      (save/restore/transition tested)

## WebUI

- [ ] Authentication incl. forced password change.
- [ ] Dashboard · [ ] Room controls · [ ] Light controls · [ ] Scene controls
- [ ] Floor-plan editor (upload, place, save, modes).
- [ ] Responsive 320 px → desktop (tested viewports).
- [ ] Dark theme · [ ] Light theme — both pass contrast checks.
- [ ] Axe: no serious/critical violations on main routes.
- [ ] Bundle budgets pass in CI (DESIGN §14.1).
- [ ] Works in mock mode **and** against a real controller.

## Automation

- [ ] Trigger engine (all trigger types exercised).
- [ ] Condition engine · [ ] Action engine.
- [ ] Simple editor · [ ] Advanced visual editor.
- [ ] Scheduler (time + sunrise/sunset).
- [ ] Execution history.
- [ ] Loop prevention (save-time + runtime tests).

## Networking

- [ ] Wi-Fi station · [ ] Wi-Fi AP (secure provisioning confirmed) ·
      [ ] Ethernet
- [ ] MQTT (incl. reconnect) · [ ] Home Assistant Discovery (create, rename,
      delete lifecycle).
- [ ] TCP · [ ] UDP (auth/rate-limit default confirmed) · [ ] ESP-NOW.
- [ ] Multi-controller synchronization — ≥2 boards, offline and
      loop-soak tests recorded.

## Security and Reliability

- [ ] Default account provisioning + forced change (cannot regress — test).
- [ ] Secure sessions (flags, timeout, invalidation).
- [ ] Secure configuration (no secrets in JSON/exports/logs — scan test).
- [ ] OTA apply + rollback drill passed.
- [ ] Backup and restore drill passed (incl. failed-restore safety).
- [ ] Watchdog on every long-running task.
- [ ] Diagnostics complete.
- [ ] Long-duration (≥24 h) test recorded.

## Final artifacts before release

- [ ] `docs/IMPLEMENTATION_STATUS.md` — every phase box checked or explicitly
      marked blocked with reason.
- [ ] Known-limitations list written (unverified hardware items, deviations
      from SPEC targets, deferred items).
- [ ] Version tagged; changelog/release notes generated.
- [ ] All pinned dependency versions unchanged from what was approved.

## Completion report template (use for the release, and per-task in smaller form)

- What was implemented (per phase).
- Modules and files changed.
- Tests run and results.
- Build results (firmware matrix, web UI, bundle sizes).
- Hardware verification status (verified / unverified / mock only — per
  resource).
- Docs updated.
- Remaining issues and limitations.
