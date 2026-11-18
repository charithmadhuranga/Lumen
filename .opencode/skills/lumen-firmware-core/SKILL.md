---
name: lumen-firmware-core
description: Core firmware work for Lumen — system manager, typed event bus, transactional config/storage (NVS + LittleFS), schema versioning and migration, structured logging, diagnostics, FreeRTOS service lifecycle and task registry. Use when implementing or changing anything under src/core/, boot order, persistence, config documents, or system-wide services. Triggers: "event bus", "config manager", "storage manager", "system manager", "services lifecycle", "diagnostics", "phase 1".
---

# Lumen — Core Firmware

**Plan:** `.opencode/plans/phase-01-core-firmware.md` (also Phase 0 scaffold).
**Spec:** SPEC §4 (architecture), §19 (storage), §21 (logging), §23 (tasks).

## Responsibilities of this layer

- `src/core/`: `system_manager`, `event_bus`, `config_manager`,
  `storage_manager`, `diagnostics` (+ `device_registry` when devices are
  registered).
- Everything else consumes these; nothing here may depend on lighting,
  networking or web code.

## Procedure

1. **Event bus first.** It is the contract every later phase builds on:
   - Typed `EventId` + fixed-size payload; bounded; explicit drop policy with
     per-priority drop counters.
   - Critical events (hardware fault, config change) never silently dropped;
     telemetry may be coalesced (SPEC §23 rules 4–6).
   - Keep the core logic FreeRTOS-free behind a small port interface so host
     tests run on `pio test -e native`.
2. **Storage = transactional.** temp file → validate → atomic rename, with
   one previous-generation backup per document. Corrupt/missing file falls
   back backup → defaults, logged, never a crash.
3. **Config pipeline:** load → `schema_version` check → migrate (additive,
   non-destructive) → validate → notify (`config_changed` event). Secrets
   are never part of these JSON documents — NVS only.
4. **Service lifecycle:** every service implements init/start/stop with
   declared dependencies and is registered in exactly one place. Tasks are
   never created ad hoc: priority + stack are declared in the registry and
   recorded in `docs/ARCHITECTURE.md` (AGENTS.md §5).
5. **Failure isolation:** one service failing hardware init must not stop
   the rest of the boot (SPEC §1.2 rule 6). Watchdogs for long loops; safe
   output states on shutdown.
6. **Logging:** categories SYSTEM, HARDWARE, LIGHTING, AUTOMATION, NETWORK,
   MQTT, ESP_NOW, WEB, OTA, SECURITY; use the redaction helper for anything
   credential-like; never log secrets.

## Validation gate (Phase 1)

- [ ] Boot is stable across repeated resets; config round-trips identically.
- [ ] Injected corruption → backup/defaults, error logged, no crash.
- [ ] Unknown `schema_version` rejected with old file intact.
- [ ] Host tests: event bus ordering/bounds/drops, transactional write,
      migration pipeline, corrupted-config recovery.
- [ ] Task registry table in `docs/ARCHITECTURE.md` matches the code.
- [ ] No dynamic allocation in the event-bus hot path; all queues bounded.

## Pitfalls

- Putting ESP-IDF types in core headers — breaks native tests.
- Persisting frequently changing runtime state to flash (wear) — config only
  (SPEC §24).
- Allowing a "just this once" unbounded queue or task spawn.
- Silently swallowing an `esp_err_t` (AGENTS.md §5).

## Completion report

What was implemented, files changed, tests + results, build results, docs
updated, remaining issues (template in `.opencode/plans/release-gate.md`).
