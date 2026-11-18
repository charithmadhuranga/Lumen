---
name: lumen-dev
description: Develop Lumen, the universal ESP32 smart lighting controller. Use for ESP-IDF firmware, board profiles, HAL and drivers (LEDC, PCA9685, PCF8574, RMT), lighting engine, automation, ESP-NOW, MQTT and Home Assistant, REST and WebSocket API, and the React web UI (shadcn/ui, Konva floor plan, React Flow automation editor). Driven by the phase plan in .opencode/plans/ with phase skills in .opencode/skills/.
---

# Lumen Development

Companion files: `AGENTS.md` (rules), `SPEC.md` (requirements), `DESIGN.md` (web UI design).

## Phase plan and project skills

- The implementation sequence lives in `.opencode/plans/`. Start with `.opencode/plans/README.md` (phase index, dependency graph, global conventions), then open the file for the current phase. Each phase file breaks the work into tasks, required tests and an exit gate — a phase is done only when its gate passes. Progress is recorded in `docs/IMPLEMENTATION_STATUS.md`.
- Phase-specific skills live in `.opencode/skills/`: `lumen-overview` (orientation), `lumen-firmware-core`, `lumen-board-hal`, `lumen-lighting-engine`, `lumen-webui`, `lumen-floorplan`, `lumen-automation`, `lumen-networking`, `lumen-security-ota`, `lumen-testing` (gate checklist). Load the skill mapped to the current phase in the plan's README before writing code.
- The final release checklist is `.opencode/plans/release-gate.md`.

## Before you start

1. Read `AGENTS.md`.
2. Read the plan file for the current phase and the skill mapped to it.
3. Read the spec sections and docs for the subsystem you are changing.
4. Read the relevant board profile in `boards/` and schema in `schemas/`.
5. For UI work, read `DESIGN.md`.
6. Look at existing code and copy its patterns.

## General procedure

1. **Understand.** List dependencies, the events involved, who owns which resource.
2. **Define the interface.** Inputs, outputs, events, errors, persisted state, resource ownership. Keep it small and explicit.
3. **Implement the smallest complete slice.** Use existing patterns. No speculative abstractions.
4. **Validate.** Build, run tests, validate schemas, check memory (firmware) or bundle size (UI).
5. **Document.** API, schema, profile, protocol and UI-workflow changes go into docs.
6. **Report.** See "Completion report" below.

## Firmware tasks

### Adding or changing a driver
- Implement the matching HAL interface (`IOutputDriver`, `IPwmDriver`, `IDigitalInputDriver`, etc.).
- Provide `initialize`, `shutdown` where relevant, range validation, and error reporting via `esp_err_t`.
- Add a mock implementation for host tests.
- PCA9685: batch pending writes, avoid redundant I2C transactions, recover from I2C errors.
- PCF8574: debounce, detect changes, emit input events, monitor device health.
- Register required resources with the pin and resource validator so conflicts are caught.

### Adding or changing a board profile
1. Write JSON conforming to `schemas/board-profile.schema.json`.
2. Run schema validation, then semantic validation (`PinValidator`): GPIO exists, direction valid, not input-only misused, not strapping or flash/PSRAM reserved without justification, no duplicates, peripheral references resolve.
3. Declare capabilities honestly.
4. Mark anything not physically verified as unverified in the profile `settings` and in `docs/HARDWARE_PROFILES.md`.
5. Built-in profiles are read-only at runtime. Users duplicate them.

### Applying a profile at runtime
Follow the transactional workflow: validate, save candidate, stop affected services, set safe states, init new hardware, run diagnostics, commit, or restore the previous profile on failure.

### Lighting engine
- Everything goes through logical channels and `LightState`. Keep requested, applied and actual state separate.
- Use the color engine. Never convert colors ad hoc.
- Transitions and effects are non-blocking and cancellable. A new command replaces them.
- Publish events on the event bus after hardware application.

### Automation engine
- Trigger matcher, condition evaluator, action executor. Event driven.
- Enforce cooldown, max executions, recursion prevention, queue bounds, max duration, cancellation.
- A failing automation logs and continues. It never crashes the service.

### Networking (MQTT, ESP-NOW, TCP, UDP, HTTP, WebSocket)
- Callbacks enqueue, workers execute.
- Validate size, version and authentication first.
- ESP-NOW: compact binary envelope, sequence numbers, replay and duplicate protection, peer limits, application-level authentication.
- TCP: explicit message framing. A read is not a message.
- MQTT and Home Assistant: stable unique IDs, retained discovery cleanup, LWT availability.
- Conflict rules: unique command IDs, stale rejection, no sync loops, local input stays responsive.

### API changes
- Keep the response envelope `{success, data, error}`.
- Update the Zod schema in `webui/src/schemas/`, the API doc, and the firmware validator together.

## Web UI tasks

### Setup facts
- Stack and rules are in `DESIGN.md`. Check whether Tailwind is v3 or v4 before following any setup snippet.
- shadcn components are added with the CLI into `src/components/ui`. Wrap rather than edit them.

### Adding a screen
1. Add a lazy route.
2. Create a feature folder with screen, hooks and local components.
3. Define Zod schemas, then query hooks in `services/queries/`.
4. Handle loading (`Skeleton`), empty (`EmptyState`), error and offline states.
5. Make it work from 320px up. Touch targets at least 44px.
6. Add component tests and, for key flows, a Playwright test with an axe check.

### Adding a light control
- Controls are driven by light type (see `DESIGN.md` 5.3). Never show unsupported controls.
- Slider commands are throttled (about 100 ms) with a final command on release. Reconcile with the next `light_state_changed` event.

### Live state
- WebSocket events patch the TanStack Query cache and `liveStore`. Do not create a second source of truth.
- Validate each message with Zod. Ignore unknown events and versions.

### Floor plan
- Control mode: sanitized inline SVG plus Motion. Edit mode: React Konva. Never mix them.
- Positions are in SVG `viewBox` space via `coords.ts`.
- Never alter the uploaded SVG. Run every SVG through the sanitizer. Add a sample to the malicious-SVG test suite when you change it.
- Animate `opacity` and `transform` only. One shared blur filter, not one per light.

### Automation editor
- Simple form first, React Flow advanced editor lazy-loaded.
- Validate connections: one trigger, no cycles, compatible handles, no orphan actions.
- Serialize to the versioned graph schema. Declarative data only.

### Performance checklist
- Named icon imports only.
- `LazyMotion` with `m` components.
- Konva, React Flow, Recharts and GSAP loaded only on their routes.
- Run the bundle-size check. Do not exceed the budget in `DESIGN.md` section 14.

## Validation checklist

Firmware:
- [ ] Compiles for every supported target in the matrix.
- [ ] Host unit tests pass with mock drivers.
- [ ] Schemas validate.
- [ ] No new unbounded queues or allocations in hot paths.
- [ ] Stack sizes and priorities documented for any new task.
- [ ] Heap impact checked.

Web UI:
- [ ] `typecheck`, `lint`, `test`, `build` pass.
- [ ] Axe reports no serious or critical issues.
- [ ] Both themes checked. Reduced motion checked.
- [ ] Bundle budgets pass.
- [ ] Works in mock API mode and against a real controller when available.

## Pitfalls

- Treating a mock test as hardware proof.
- Assuming the PCA9685 is at `0x40` on the KC868-AP, or that relay and input polarities are what the pinmap implies.
- Using strapping pins (0, 2, 12, 15 on the original ESP32) or flash pins without checking.
- Doing slow work in an HTTP, MQTT or ESP-NOW callback.
- Writing I2C from many places instead of the batching driver.
- Holding the same server data in both Query and Zustand.
- Loading a heavy library on the initial route.
- Rendering floor-plan lights from local guesses instead of device state.
- Exposing a secret through an API response, log or backup.

## Completion report

End each task with:

- What was implemented.
- Modules and files changed.
- Tests run and results.
- Build results.
- Hardware verification status (verified, unverified, mock only).
- Docs updated.
- Remaining issues and limitations.
