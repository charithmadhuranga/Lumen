# Implementation Plan — Lumen

This directory holds the phase-by-phase implementation plan for the system
described in `SPEC.md` (product + firmware) and `DESIGN.md` (WebUI).

**Source of truth:** `SPEC.md` and `DESIGN.md` win on any conflict with these
plan files. `AGENTS.md` holds the non-negotiable architecture rules. These plan
files only sequence the work and make each phase's tasks, tests and gates
explicit.

## How to use this plan

1. Work **one phase at a time, in order**. Do not start a later phase's heavy
   work while an earlier phase is incomplete (SPEC §29 Rule 1–3).
2. Before starting a phase, load the mapped skill(s) from `.opencode/skills/`
   and read the phase file end to end.
3. Execute the tasks as checkboxes. Track canonical status in
   `docs/IMPLEMENTATION_STATUS.md` (created in Phase 0) — the plan files are
   the task breakdown, that file is the progress record.
4. A phase is only finished when its **gate** (acceptance criteria + validation
   checklist) passes. Every gate includes, at minimum:
   - Firmware compiles for every env in the target matrix.
   - Host unit tests pass.
   - Web UI typechecks, lints, tests and builds (from Phase 6 on).
   - Schemas validate.
   - Memory (firmware) / bundle size (UI) reviewed.
   - Documentation and `docs/IMPLEMENTATION_STATUS.md` updated.
5. Keep the repository compiling between milestones. Never mark a task done
   because code exists — a driver that has never talked to its peripheral is
   "mock only", not "verified" (SPEC §29 Rule 5, 7).

## Phase dependency graph

```text
Phase 0  Foundation (build systems, skeleton, CI, docs)
   |
Phase 1  Core firmware (event bus, config/storage, diagnostics, services)
   |
Phase 2  Hardware abstraction (HAL, profiles, PinValidator, P0 drivers)
   |
Phase 3  KC868-AP physical bring-up (needs real hardware)
   |
Phase 4  Lighting engine (channels, color, transitions, effects)
   |
Phase 5  Rooms, groups, scenes
   |
   +-------------------+
   |                   |
Phase 6              (firmware REST/WS/auth land here)
WebUI foundation
   |
   +--------+--------+
   |                 |
Phase 7            Phase 8
Hardware UI        Floor plan
   |                 |
   +--------+--------+
            |
         Phase 9  Automation engine + editors
            |
         Phase 10 Networking (Wi-Fi/Ethernet/MQTT/HA/TCP/UDP)
            |
         Phase 11 Multi-controller (ESP-NOW + sync/conflict resolution)
            |
         Phase 12 Security, OTA, backup, hardening, reliability
            |
         Release gate (SPEC §30 Definition of Done)
```

Phases 7 and 8 are independent of each other and may be interleaved. Phases
10–11 depend on Phase 6 only for their configuration screens; the protocol
firmware itself depends on Phase 1's event bus.

## Phase index and skill mapping

| Plan file | Phase (SPEC §26) | Primary skill(s) in `.opencode/skills/` |
|---|---|---|
| `phase-00-foundation.md` | 0 — Repository and project foundation | `lumen-overview` |
| `phase-01-core-firmware.md` | 1 — Core firmware | `lumen-firmware-core` |
| `phase-02-hardware-abstraction.md` | 2 — Hardware abstraction | `lumen-board-hal` |
| `phase-03-kc868-ap-hardware.md` | 3 — KC868-AP hardware validation | `lumen-board-hal` |
| `phase-04-lighting-engine.md` | 4 — Lighting engine | `lumen-lighting-engine` |
| `phase-05-rooms-groups-scenes.md` | 5 — Rooms, groups, scenes | `lumen-lighting-engine` |
| `phase-06-webui-foundation.md` | 6 — WebUI foundation | `lumen-webui` |
| `phase-07-hardware-configuration-ui.md` | 7 — Hardware configuration UI | `lumen-webui`, `lumen-board-hal` |
| `phase-08-floorplan-editor.md` | 8 — Floor-plan editor | `lumen-floorplan` |
| `phase-09-automation.md` | 9 — Automation engine | `lumen-automation` |
| `phase-10-networking.md` | 10 — Networking | `lumen-networking` |
| `phase-11-multi-controller.md` | 11 — Multi-controller synchronization | `lumen-networking` |
| `phase-12-security-production.md` | 12 — Security and production | `lumen-security-ota` |
| `release-gate.md` | §30 Definition of Done | `lumen-testing` |

Cross-cutting testing procedure lives in `lumen-testing` and applies to
every phase gate.

## Global conventions

- **Repository layout:** SPEC §22 (target tree). Adjust file boundaries when
  necessary, but never break the layer separation: `core/`, `hardware/`,
  `lighting/`, `automation/`, `networking/`, `web/`, `services/`.
- **Commands:** see the table in `AGENTS.md` §3. Keep it accurate as scripts
  land.
- **API envelope:** every REST response is `{success, data, error}` (SPEC
  §17.1). Every WebSocket message uses the versioned envelope
  `{version, event, timestamp, data}` (SPEC §17.3).
- **Zod owns API types** in `webui/src/schemas/`; firmware validators and the
  Zod schema change together in the same commit.
- **No board GPIO in application code.** Pins exist only in `boards/*.json`
  (AGENTS.md §4.1).
- **Unverified hardware stays marked unverified** until a physical test proves
  it — especially the KC868-AP PCA9685 address (`0x00` per `pinmap.txt`,
  `0x40` in vendor examples — scan and confirm, never guess) and
  PCF8574 addresses/polarities (SPEC §6.3, AGENTS.md §6).
- **Dependencies:** ask before adding one (AGENTS.md §15). Pin versions; never
  upgrade pinned versions unasked.
