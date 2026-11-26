# Release Gate — Definition of Done (SPEC §30)

**Skill:** `lumen-testing`

Run this gate after Phase 12. Every box must be checked with **evidence**
(test result, hardware run record, or measurement) before calling the first
production-ready release done. A box backed only by code that was never run
does not count (SPEC §29 Rule 7).

## Status for v1.0.0-rc.1 (2026-11-26)

`[x]` = done with evidence, `[ ]` = open (reason given), `[-]` = deferred or
blocked by a recorded decision. "HW" means it was run on a real board (KC868-AP
`lumen-0798` unless another board is named), "host" means host unit tests.
**Result: release candidate only. Open items block 1.0.0:** the 24 h soak, and the
hardware checks for Ethernet, RS485 and relays, or the owner's explicit acceptance
of those items as known limitations.

## Hardware

- [x] KC868-AP preset — PCA9685 0x40, PCF8574 0x21/0x22 and GPIO inputs verified
      2026-11-25 (`docs/HARDWARE_PROFILES.md`, IMPLEMENTATION_STATUS hardware log).
- [x] Generic ESP32 preset — HW: ESP32 DevKit `lumen-e704` runs `generic_esp32`
      (GPIO LED switch, 3 LEDC outputs, I²C bus initialised); multi-controller suite.
- [ ] Generic ESP32-S3 preset — compile-only (release CI builds it); no S3 board tested.
- [x] Custom board editor — create/duplicate/import/export/validate/apply in the
      Hardware UI; validation and rollback host-tested. Applying a deliberately failing
      profile has not been exercised on hardware.
- [x] Pin conflict detection — `test_host_pin_validator` (green, 197/197 host tests).
- [x] PCA9685 — address 0x40, every channel verified by register read-back (HW); fault
      and recovery verified with injected I²C faults (`reliability_test.py`, HW 20/20).
- [x] PCF8574 — 0x22 (DI1–8), 0x21 (DI9–16), active-low; HW 2026-11-25.
- [x] Native PWM (LEDC) — HW on `lumen-e704` 2026-11-26: dimmer on `ledc_pwm0` gave
      100/18.4/1.1/0 % for 100/50/10/0 % brightness (perceptual curve), and a 1.5 s fade
      was mid-way at 0.75 s. The read-back is the driver's latched duty; nothing physical
      was attached.
- [ ] Relay — toggling verified (GPIO13/GPIO2); **polarity and boot state with a load
      not verified** (no mains load used, by rule).
- [-] RMT addressable LEDs — not implemented (no strip available); `rmt` lights are
      refused with an error. Listed as a known limitation.
- [ ] Ethernet — code path present, opt-in; **not tested** (no cable/link run recorded).
- [ ] RS485 — **not tested**.

## Lighting

- [x] Single-channel dimming (HW, PCA9685 + LEDC) · [x] Relay lighting (HW) · [x] RGB (HW
      read-back) · [x] RGBW (host + PCA read-back via the RGB/RGBW adapter) · [x] CCT (HW
      read-back) · [-] Addressable LEDs (see RMT)
- [x] Effects (all 9) — host `test_host_engine`; breathing on the strip observed by the
      owner (HW) · [x] Transitions — non-blocking (review: no delays in the lighting
      task); timing: 2 s fade observed, mid-fade read-back on LEDC; on-device command
      latency 13 ms on average, 28 ms at most.
- [x] Rooms (HW e2e) · [x] Groups (mixed-type fan-out host-tested) · [x] Scenes —
      save/activate with transition host-tested; capture + network-wide activation HW
      (`multi_test.py`).

## WebUI

- [x] Authentication incl. forced password change — HW (e2e, security suite).
- [x] Dashboard · [x] Room controls · [x] Light controls · [x] Scene controls — e2e on HW.
- [x] Floor-plan editor — upload, place (click and drag-from-tray), glow-radius handle,
      save, Control/Edit modes: e2e on HW (desktop + mobile).
- [ ] Responsive 320 px → desktop — e2e runs at 360 px (mobile) and 1280 px; **320 px not
      in the automated set**.
- [x] Dark theme · [x] Light theme — axe colour-contrast passes in both (e2e).
- [x] Axe: no serious/critical violations on the main routes (e2e, both themes).
- [x] Bundle budgets — `npm run check:budget` in CI and release CI: initial 159.6/160 KB,
      CSS 13.3/25, total 483.7/500, viewer 33.1/40, editor 83.5/90 (KB gz).
- [ ] Works in mock mode **and** against a real controller — real controller yes; **MSW
      mock-API mode not implemented** (unit tests stub `fetch`).

## Automation

- [x] Trigger engine — time, sun, input, light, MQTT and variable triggers (host
      `test_host_automation`); input/light/time on HW e2e.
- [x] Condition engine · [x] Action engine — host + HW e2e.
- [x] Simple editor · [x] Advanced visual editor (React Flow) — e2e on HW.
- [x] Scheduler (time + sunrise/sunset) — host (solar maths, schedules).
- [x] Execution history — HW e2e.
- [x] Loop prevention — save-time validation and runtime chain depth (host + HW).

## Networking

- [x] Wi-Fi station (HW) · [x] Wi-Fi AP (WPA2, unique per-device password, no open
      management hotspot; used on HW) · [ ] Ethernet (not tested)
- [x] MQTT incl. reconnect — HW 2026-11-26: broker stopped → disconnect seen at once;
      restarted → reconnected in 3 s, publishing resumed · [x] Home Assistant discovery —
      create, rename, delete lifecycle against HA 2026.11.2 (`ha_test.py` 34/34, HW).
- [x] TCP · [x] UDP (HMAC auth, replay/timestamp checks, 20/s rate limit; HW) ·
      [x] ESP-NOW (HW, 2 boards).
- [x] Multi-controller synchronisation — 2 boards: `multi_test.py` 17/17 (sync, scenes,
      wrong key, broker down, 20 s storm with no amplification, restart) and
      `reliability_test.py` (peer loss → offline in 17 s, 409 for remote commands,
      rejoin in 1 s, late remote command rejected as stale).

## Security and Reliability

- [x] Default account provisioning + forced change — HW + security suite (cannot be
      skipped: 98 routes refuse a must-change session).
- [x] Secure sessions — HttpOnly + SameSite=Strict cookie, CSRF header, logout and
      WebSocket invalidation (security suite 29/29).
- [x] Secure configuration — secrets scan of every GET, diagnostics and backup
      (`security_test.py --secrets`); secrets only in NVS. Note: a fresh device prints
      its setup-hotspot password on the serial console (provisioning path).
- [x] OTA apply + rollback drill — valid update, corrupt/truncated/wrong images refused,
      crash → bootloader rollback, unhealthy → health-check rollback (HW).
- [x] Backup and restore drill incl. failed-restore safety (HW + host: hostile input,
      interrupted swap).
- [x] Watchdog on every long-running task — `lighting`, `housekeeping`, `espnow` and
      `ctrl` are on the task watchdog (5 s). ESP-IDF's own `httpd`, `mqtt_task` and Wi-Fi
      tasks are not ours. The watchdog reports a hang; it does not reset the device
      (`CONFIG_ESP_TASK_WDT_PANIC` is off).
- [x] Diagnostics complete — SPEC §21 fields plus failed allocations and peer status.
- [ ] Long-duration (≥24 h) test — **not done**: 1.1 h run stopped by the owner
      (no reboot, flat heap). `soak_test.py --hours 24` must pass before 1.0.0.

## Final artifacts before release

- [x] `docs/IMPLEMENTATION_STATUS.md` — every phase box checked or marked open with a
      reason.
- [x] Known-limitations list written (IMPLEMENTATION_STATUS, CHANGELOG).
- [x] Version tagged (`v1.0.0-rc.1`); changelog in `CHANGELOG.md`; release notes are
      generated from it by `.github/workflows/release.yml`.
- [x] Pinned versions unchanged: `espressif32@8.0.0` (ESP-IDF 6.1.0), PlatformIO 6.2.0 in
      CI. Web dependencies added during the project were each approved (konva,
      react-konva). The release CI adds `esptool==4.8.1` for merging images (CI only,
      nothing ships to the device).

## Completion report template (use for the release, and per-task in smaller form)

- What was implemented (per phase).
- Modules and files changed.
- Tests run and results.
- Build results (firmware matrix, web UI, bundle sizes).
- Hardware verification status (verified / unverified / mock only — per
  resource).
- Docs updated.
- Remaining issues and limitations.
