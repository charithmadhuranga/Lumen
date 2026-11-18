# Phase 1 — Core Firmware

**SPEC §26 Phase 1, SPEC §4, §19, §21, §23** · **Skill:** `lumen-firmware-core`

## Outcome

The skeleton application: a system manager that boots a registry of services,
a typed bounded event bus, transactional configuration/storage managers on
NVS + LittleFS, structured logging and diagnostics. Device boots reliably,
config survives reboot, invalid config is rejected, services start/stop
cleanly.

## Dependencies

Phase 0.

## Tasks

### 1.1 Event bus (`src/core/event_bus.*`)

- [ ] Typed, bounded, publish/subscribe bus. Events carry an enum id + a
      fixed-size payload union or small struct — no heap strings in hot paths.
- [ ] Subscriptions registered at service init; publish is non-blocking for
      producers (drop policy is explicit and observable: counters for dropped
      events by priority, SPEC §23 rule 4–6).
- [ ] Critical events (hardware fault, config change) are never silently
      dropped; telemetry events may be coalesced.
- [ ] Host-testable: no ESP-IDF primitives in the core logic (wrap queue
      behind a small port interface for native tests).

### 1.2 Storage manager (`src/core/storage_manager.*`)

- [ ] NVS namespace handling for secrets/critical settings; LittleFS mount and
      file access for JSON config (SPEC §19).
- [ ] Transactional write pattern: write temp file → validate → atomic
      rename; keep one previous-generation backup of each document
      (SPEC §19.3 recovery).
- [ ] Corrupted/missing file behavior: fall back to backup, then to defaults,
      and log — never crash (SPEC §26 Phase 1 acceptance).

### 1.3 Configuration manager (`src/core/config_manager.*`)

- [ ] Document registry per SPEC §19.2 file structure (`system.json`,
      `active_board.json`, `lights/`, `rooms/`, `groups/`, `scenes/`,
      `automations/`, `floorplans/`, `integrations/`, `backups/`).
- [ ] Load → schema-version check → migrate → validate pipeline with a
      migration table (additive, non-destructive — AGENTS.md §4.10).
- [ ] Secrets are **never** stored in these JSON documents (NVS only).
- [ ] Change notification through the event bus (`config_changed`).

### 1.4 System manager and service lifecycle (`src/core/system_manager.*`)

- [ ] Declarative service registry: each service exposes `init/start/stop`
      with explicit dependency ordering; register in one place so startup
      order is auditable.
- [ ] FreeRTOS task policy: no task is created implicitly by a service —
      tasks are declared with priority + stack size in the registry table and
      documented in `docs/ARCHITECTURE.md` (AGENTS.md §5).
- [ ] Watchdog registration for long-running service loops; clean shutdown
      path (safe output states on stop).
- [ ] Bounded startup: hardware init failure in one service must not prevent
      the rest of the system from booting (SPEC §1.2 rule 6).

### 1.5 Logging and diagnostics (`src/core/diagnostics.*`)

- [ ] Structured log categories per SPEC §21: SYSTEM, HARDWARE, LIGHTING,
      AUTOMATION, NETWORK, MQTT, ESP_NOW, WEB, OTA, SECURITY.
- [ ] Secret redaction guard: a helper that never logs credentials/tokens
      (AGENTS.md §5).
- [ ] Runtime stats collection: free heap, min free heap, task watermarks,
      uptime, reset reason — exposed via a diagnostics snapshot API consumed
      later by REST.

## Contracts & interfaces introduced

- `EventBus` API: `subscribe(EventId, handler)`, `post(Event, Priority)` —
  stable; everything in later phases rides on it.
- `IStorage` / `IConfigStore` interfaces (mockable on host).
- Service interface (`IService`: init/start/stop + name) used by every later
  module.
- `docs/ARCHITECTURE.md` task registry table filled with any task created
  here.

## Gate (acceptance criteria)

1. Device boots reliably to a stable idle state; repeated resets do not
   corrupt config.
2. Config write → reboot → config identical (round-trip test).
3. Truncated/corrupted JSON → backup or defaults used, error logged, no
   crash (inject via host test).
4. Unknown `schema_version` → rejected with a clear error, old file left
   intact.
5. Services start and stop in declared order; stop puts registered outputs
   into configured safe states.
6. Host unit tests cover: event bus (ordering, bounds, drop counters),
   transactional writes, migration pipeline, corrupted-config recovery.
7. Task registry documented in `docs/ARCHITECTURE.md` (priority + stack for
   every task created).

## Docs to update

`docs/ARCHITECTURE.md` (event bus, service lifecycle, task registry),
`docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- Event payload design constrains every later phase — keep it fixed-size and
  versioned; revisit only before Phase 4.
- LittleFS wear from frequent state writes: persist configuration, not
  per-frame runtime state (SPEC §24).
