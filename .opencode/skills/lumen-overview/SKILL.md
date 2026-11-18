---
name: lumen-overview
description: Orientation and phase-plan navigation for the Lumen repo (universal ESP32 smart lighting controller: ESP-IDF firmware + React WebUI, KinCony KC868-AP reference). Use for ANY task in this repository — first contact with the project, deciding what to implement next, understanding layer rules, finding the right plan phase or sibling skill, or reporting completion. Triggers: "esp lighting", "smart lighting controller", "lumen", "kc868", "start phase", "what to do next", "implementation plan".
---

# ESP Lighting — Project Overview

You are working in the Lumen repository (universal ESP32 smart lighting controller).
Load this skill first for any task here; load the phase-specific skill listed
below for the actual work.

## Read order (before writing code)

1. `AGENTS.md` — non-negotiable architecture/security/testing rules.
2. `.opencode/plans/README.md` — phase index, dependency graph, conventions.
3. The plan file for the current phase (table below).
4. The mapped skill(s) for that phase.
5. The cited sections of `SPEC.md` (product + firmware) and, for UI work,
   `DESIGN.md` (authoritative for the WebUI).
6. Existing code — copy its patterns; do not introduce new abstractions
   without need.

## The plan

Sequential phases live in `.opencode/plans/` (SPEC §26). A phase is finished
only when its gate passes. Track canonical progress in
`docs/IMPLEMENTATION_STATUS.md` (created in Phase 0).

| Phase | Plan file | Skill |
|---|---|---|
| 0 Foundation | `phase-00-foundation.md` | this skill |
| 1 Core firmware | `phase-01-core-firmware.md` | `lumen-firmware-core` |
| 2 Hardware abstraction | `phase-02-hardware-abstraction.md` | `lumen-board-hal` |
| 3 KC868-AP validation | `phase-03-kc868-ap-hardware.md` | `lumen-board-hal` |
| 4 Lighting engine | `phase-04-lighting-engine.md` | `lumen-lighting-engine` |
| 5 Rooms/groups/scenes | `phase-05-rooms-groups-scenes.md` | `lumen-lighting-engine` |
| 6 WebUI foundation | `phase-06-webui-foundation.md` | `lumen-webui` |
| 7 Hardware config UI | `phase-07-hardware-configuration-ui.md` | `lumen-webui` + `lumen-board-hal` |
| 8 Floor plan | `phase-08-floorplan-editor.md` | `lumen-floorplan` |
| 9 Automation | `phase-09-automation.md` | `lumen-automation` |
| 10 Networking | `phase-10-networking.md` | `lumen-networking` |
| 11 Multi-controller | `phase-11-multi-controller.md` | `lumen-networking` |
| 12 Security/production | `phase-12-security-production.md` | `lumen-security-ota` |
| Release | `release-gate.md` | `lumen-testing` |

Cross-cutting test procedure: `lumen-testing`.

## Architecture in one paragraph

React SPA (served gzipped from LittleFS) ⇄ HTTP REST + WebSocket
(`/api/v1/ws`, versioned envelope) ⇄ web layer ⇄ application core
(lighting engine, automation engine, device registry) ⇄ typed bounded event
bus ⇄ hardware manager (HAL drivers, board profiles, PinValidator) / network
manager (Wi-Fi, Ethernet, MQTT, ESP-NOW, TCP, UDP) / storage manager (NVS
secrets + LittleFS transactional JSON). No application module contains GPIO
numbers; pins exist only in `boards/*.json`. Network callbacks only enqueue.

## Standing rules that bite most often

- One phase at a time; keep the repo compiling; no fake completion
  (SPEC §29).
- Hardware facts are unverified until physically tested — especially
  KC868-AP PCA9685 address (`0x00` vs `0x40`), PCF8574 addresses, relay/
  input polarity, Ethernet clock, RS485 direction. Mark, don't guess.
- Secrets live in NVS only; never in JSON, exports, logs or API responses.
- Callbacks enqueue; lighting code never blocks; newer command replaces an
  active transition/effect.
- WebUI: Zod is the API type source of truth; TanStack Query owns REST data,
  Zustand owns live/UI state; heavy libs lazy-loaded; no external requests;
  design tokens only (DESIGN.md).
- Ask before: adding a dependency, changing partitions/toolchain, migrating a
  persisted schema, anything that erases flash or changes security defaults.

## Working method (every task)

1. Inspect code + read the phase plan + relevant SPEC/DESIGN sections.
2. Identify affected modules; plan briefly.
3. Implement the smallest complete slice using existing patterns.
4. Add tests (host/mocks for firmware; Vitest/RTL/MSW/Playwright/axe for UI).
5. Build and validate — see `lumen-testing` for the gate checklist.
6. Update docs (`docs/*`, `docs/IMPLEMENTATION_STATUS.md`).
7. Report using the completion-report template in `release-gate.md`.
