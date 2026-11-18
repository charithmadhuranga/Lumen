# Phase 9 — Automation Engine and Editors

**SPEC §26 Phase 9, SPEC §11; DESIGN §7.6, §10** · **Skill:** `lumen-automation`

## Outcome

A local trigger-condition-action engine that runs with no internet, plus two
editors: a simple form anyone can use and a React Flow graph editor for
complex logic. Loops are impossible, executions are bounded and logged.

## Dependencies

Phases 1 (events/scheduler host), 4–5 (lights/scenes/actions), 6 (web
stack). Inputs/triggers need Phase 2's input events; time triggers need a
time service (`src/services/time_manager.*`, NTP when online, local clock
otherwise).

## Tasks

### 9.1 Engine core (`src/automation/`)

- [ ] `trigger_manager.*`: trigger types from SPEC §11.2 (light/brightness/
      color change, input state, button press/double/long, time schedule,
      sunrise/sunset, sensor threshold, MQTT event, ESP-NOW event, scene
      activated, group changed, controller startup, timer expired). All fed
      from bus events — no polling loops.
- [ ] `condition_evaluator.*`: operators from SPEC §11.3 (eq/ne/gt/lt/ge/le,
      AND/OR/NOT trees, time range, day of week, state comparison). Pure
      function over context → host-tested.
- [ ] `action_executor.*`: actions from SPEC §11.4 (light on/off/brightness/
      color/CCT, scene, group, relay toggle, effect run/stop, delay,
      MQTT publish, UDP send, TCP send, execute automation, set variable).
      All actions go through public engine/network APIs — never touch
      hardware directly.
- [ ] `rule_engine.*`: wires trigger → conditions → actions with execution
      settings (SPEC §11.5): `cooldown_ms`, `max_executions_per_hour`,
      `allow_parallel`.
- [ ] `scheduler.*`: time and sunrise/sunset triggers; tick-based, no drift,
      re-armed after clock resync.
- [ ] `automation_logger.*`: bounded execution history (result, duration,
      error) per automation → REST `GET /automations/{id}/history`.

### 9.2 Execution safety (SPEC §11.8) — build with the engine, not after

- [ ] Recursion/loop detection: execute-automation actions tracked on an
      execution stack with a depth cap; graph-level cycle check at save time.
- [ ] Bounded execution queue, max execution duration watchdog, per-hour rate
      limit, cooldown, cancellation on disable/delete.
- [ ] A failing automation logs and continues — the service never crashes.
- [ ] Persistence: `/config/automations/*.json` with `schema_version`,
      enabled state restored on boot, validation before enable.

### 9.3 Firmware automation endpoints

- [ ] SPEC §17.2: CRUD, `/enable`, `/disable`, `/test` (runs once, reports),
      `/history`. Bus events published: `automation_executed`.
- [ ] Validation on save mirrors the engine's own rules (unknown ids, illegal
      properties, cycles) → typed errors for the UI.

### 9.4 Simple editor (webui)

- [ ] RHF + Zod stepper sections: Trigger → Conditions (optional) → Actions
      → Execution settings; each section selects a type and renders its
      fields (DESIGN §7.6).
- [ ] Plain-language summary at the top, live-updating.
- [ ] History drawer (result, duration, error) and **Test** button with toast
      result.
- [ ] Fully usable on mobile.

### 9.5 Advanced editor (React Flow)

- [ ] Lazy-loaded tab; React Flow styles imported only there.
- [ ] Node types (DESIGN §10): Trigger, Condition, AND, OR, Delay, Light,
      Group, Scene, Relay, MQTT, UDP, TCP, Variable, Automation execution —
      shadcn Card styling, category-colored headers, Lucide icons, inline
      Zod-validated fields.
- [ ] Palette (searchable `command` + drag, `/` or double-click to add).
- [ ] `isValidConnection` rules: one trigger per graph, triggers have no
      inputs, actions cannot feed triggers, no cycles (DFS), type-compatible
      handles, no orphan actions at save. Red handle highlight + toast on
      invalid drop.
- [ ] Controls: minimap, zoom, fit view, snap, undo/redo, left-to-right auto
      layout.
- [ ] Serialize to the **versioned automation graph schema** — declarative
      data only, never code; the engine never executes user JavaScript
      (SPEC §11.7).
- [ ] Simple ↔ Advanced interop: simple automations open in Advanced; graphs
      not representable as a form open only in Advanced with a notice.
- [ ] Mobile: canvas view/pan-only with a notice.

### 9.6 Tests

- [ ] Host: trigger matching (every type), condition trees, action dispatch,
      cooldown/rate/recursion/queue caps, delay + cancellation, persistence
      round-trip, automation failure isolation (faulty action → logged, service
      alive).
- [ ] Graph validation unit suite incl. malicious/invalid graphs (cycles,
      orphan actions, type-mismatched handles, oversized graphs).
- [ ] Playwright: create simple automation → test run → history; advanced
      graph build (desktop) with an invalid-connection case; axe.

## Contracts & interfaces introduced

- Automation document + graph schema (versioned) — documented in
  `docs/AUTOMATION.md`, Zod schema added alongside firmware validator.
- `automation_executed` WS event shape.

## Gate (acceptance criteria)

1. Automations run fully offline (pull the network — time range, input
   trigger, scene action still work).
2. Both editors produce valid rules accepted by the same firmware validator.
3. Loop prevention works: self-referencing and mutual automations are blocked
   at save and bounded at runtime (tests).
4. Execution history available via API and UI.
5. Engine survives injected failures (bad action id, queue overflow) without
   crashing or leaking tasks.
6. Advanced editor stays out of the initial bundle; budgets hold.

## Docs to update

`docs/AUTOMATION.md` (trigger/condition/action reference, safety limits,
graph schema), `docs/API.md`, `docs/UI_UX.md`,
`docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- Sunrise/sunset needs lat/long: source from network config if present,
  otherwise require manual offset — document the behavior honestly.
- MQTT/UDP/TCP actions depend on Phase 10: implement their executor hooks
  behind a stub that returns `NOT_AVAILABLE` until the networking phase
  lands, and do not mark them done before then.
