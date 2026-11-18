---
name: lumen-testing
description: Testing and phase-gate procedure for Lumen — host unit tests with mock drivers, hardware test discipline (verified vs mock), firmware integration tests, web UI tests (Vitest/RTL/MSW/Playwright/axe), schema validation, memory and bundle-size review, and the checklist that closes every phase. Use whenever adding tests, running validation before finishing a phase, or deciding whether a phase/feature is truly done. Triggers: "test", "unit test", "playwright", "axe", "msw", "phase gate", "validation checklist", "bundle size", "definition of done", "mock vs hardware".
---

# ESP Lighting — Testing & Phase Gates

**Plans:** every `phase-*.md` gate section; `.opencode/plans/release-gate.md`.
**Spec:** SPEC §25 (testing strategy), §29 (execution rules).

## Honesty rules (first)

- A mock test proves logic, **never hardware**. Hardware results are recorded
  separately with date + board in `docs/HARDWARE_PROFILES.md` /
  `docs/IMPLEMENTATION_STATUS.md` and reported as: `verified` / `unverified`
  / `mock only` (SPEC §29 Rule 5).
- A feature is not done because code exists — implementation + validation +
  tests + passing build + updated docs + written limitations (AGENTS.md §16).
- Never mark a phase box checked without evidence (test output, measurement,
  hardware run record).

## Firmware tests

- **Host (`pio test -e native`)** — logic with no ESP-IDF dependency runs
  against mocks in `test/unit/mocks/` (HAL fakes, I2C bus simulator with
  error injection, fake PCA9685/PCF8574 register models). Dedicated suites
  required for: pin validation, board-profile parse/apply/rollback, config
  migration and corruption recovery, color conversion, transitions, group
  aggregation, scene apply, automation triggers/conditions/safety, command
  serialization, ESP-NOW message validation, TCP framing.
- **Integration** (`test/integration/`): REST auth → command → WS
  propagation; MQTT command/state round-trip with a broker harness; OTA;
  config rollback; Wi-Fi fallback.
- **Hardware** (`test/hardware/`): repeatable checklists per board
  (KC868-AP list in SPEC §25.2; generic ESP32/S3 lists) — run on real
  hardware only, results recorded honestly.
- **Reliability** (SPEC §25.6): disconnects, peripheral failure, corrupted
  config, queue overflow, low memory, command floods, long transitions, OTA
  interruption.
- Always also: `esp_err_t` checked, no unbounded queues added, new task
  priority/stack documented, heap impact reviewed.

## Web UI tests

- Unit: Vitest + RTL for components, forms, throttling, `coords.ts`,
  sanitizer, graph validation, envelope handling.
- API mocking: MSW REST + mock WebSocket — flows must pass in mock mode.
- E2E: Playwright — auth + forced password change, light control, scenes,
  floor-plan edit/control, automation simple + advanced, hardware apply +
  rollback, mobile viewport.
- Accessibility: axe on every main route in light **and** dark; no serious
  or critical violations.
- Mandatory malicious-input suites: SVG sanitizer samples (script, onload,
  javascript: href, external image, foreignObject, @import, oversize) and
  automation graph validation (cycles, orphans, type mismatches).

## The phase gate checklist (run before claiming any phase complete)

```text
[ ] pio run -e esp32 && pio run -e esp32s3        (firmware compiles)
[ ] pio test -e native                             (host tests pass)
[ ] npm run validate:schemas                       (schemas valid)
[ ] cd webui: lint && typecheck && test && build   (from Phase 6 on)
[ ] webui: test:e2e + axe (for touched main screens)
[ ] memory review: heap impact, no new unbounded queues/allocs in hot paths
[ ] bundle budgets pass (from Phase 6; DESIGN §14.1)
[ ] docs updated: docs/* + docs/IMPLEMENTATION_STATUS.md
[ ] hardware status of any touched peripheral reported honestly
[ ] known limitations written down
```

Then report with the completion template from
`.opencode/plans/release-gate.md`: implemented, files changed, tests +results,
build results, hardware verification status, docs updated, remaining issues.

## Pitfalls

- Skipping the gate because "the build passed once".
- Testing only the happy path of a validator — the malicious/invalid input
  suites are mandatory (AGENTS.md §11).
- Asserting against hardcoded colors/labels that tokens or i18n will change.
- Recording a hardware check you did not actually run.
