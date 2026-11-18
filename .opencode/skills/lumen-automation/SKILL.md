---
name: lumen-automation
description: Automation work for Lumen — local trigger-condition-action engine (triggers, condition evaluator, action executor, scheduler, execution history), execution safety (loop/recursion prevention, cooldowns, rate limits), the simple form editor, and the React Flow advanced graph editor. Use for anything under src/automation/, automation REST endpoints, or webui automations features. Triggers: "automation", "trigger", "condition", "action", "scheduler", "react flow", "node editor", "rule engine", "phase 9".
---

# ESP Lighting — Automation

**Plan:** `.opencode/plans/phase-09-automation.md`.
**Spec:** SPEC §11; DESIGN §7.6, §10.

## Architecture

```text
bus EVENT → trigger matcher → condition evaluator → action executor → event/log
```

- The engine is **local and event-driven**: triggers subscribe to bus events
  (light/input/scene/group changes, button press/double/long, time, sunrise/
  sunset, sensor threshold, MQTT/ESP-NOW events, startup, timers). No polling
  loops, no internet required.
- Conditions (SPEC §11.3): comparison ops, AND/OR/NOT trees, time range, day
  of week, state comparison — a pure function over context, host-tested.
- Actions (SPEC §11.4): light/group/scene/relay/effect control, delay, MQTT
  publish, UDP/TCP send, execute automation, set variable. Actions call
  **public engine/network APIs only** — never touch hardware or GPIO.

## Execution safety — build it with the engine, not after

- Recursion/loop detection: execution stack with depth cap for
  "execute automation" actions + graph-level cycle check at save time.
- Per-automation `cooldown_ms`, `max_executions_per_hour`, `allow_parallel`
  (SPEC §11.5); bounded execution queue; max execution duration watchdog;
  cancellation on disable/delete.
- A failing automation logs and continues — the service never crashes.
- **Never execute user-supplied JavaScript or shell commands** (SPEC §11.7).
- Validation before enable: unknown ids, unsupported properties, cycles →
  typed errors the UI can render inline.

## Persistence

`/config/automations/*.json` through the transactional config store with
`schema_version`; enabled state restored on boot; bounded execution history
(result, duration, error) served by `GET /automations/{id}/history`.

## Editors

- **Simple (RHF + Zod):** sections Trigger → Conditions (optional) → Actions
  → Execution settings; each selects a type and renders its fields; live
  plain-language summary at the top; history drawer; **Test** button runs
  once and reports via toast; fully usable on mobile.
- **Advanced (React Flow, lazy tab):** node types Trigger, Condition, AND,
  OR, Delay, Light/Group/Scene/Relay/MQTT/UDP/TCP action, Variable,
  Automation execution — shadcn Card styling, category-colored headers,
  inline Zod validation. `isValidConnection`: one trigger per graph,
  triggers have no inputs, actions can't feed triggers, no cycles (DFS),
  type-compatible handles, no orphan actions at save. Invalid drop → red
  handle + toast. Controls: minimap, zoom, fit, snap, undo/redo, layered
  auto-layout. Mobile: view/pan-only with a notice.
- Serialize to the **versioned graph schema** — declarative data only
  (version, nodes, edges); simple automations can open in Advanced, graphs
  not representable as a form open only in Advanced with a notice.

## Validation gate

- [ ] Automations run with the network fully down (input trigger + time
      range + scene action test).
- [ ] Both editors produce rules accepted by the same firmware validator.
- [ ] Loop prevention tests: self-referencing and mutual automations blocked
      at save, bounded at runtime.
- [ ] Cooldown / rate limit / queue cap / max-duration / cancellation each
      have a test.
- [ ] Faulty action → logged, automation continues, service alive.
- [ ] Persistence round-trip after reboot.
- [ ] Graph-validation suite covers invalid/malicious graphs (cycles,
      orphans, type mismatches, oversized input).
- [ ] Playwright: simple automation end-to-end + advanced invalid-connection
      case; axe pass; advanced chunk within the 100 KB budget.

## Pitfalls

- Actions that run inline in the trigger callback (must enqueue).
- MQTT/UDP/TCP actions stubbed forever — until Phase 10 they return
  `NOT_AVAILABLE`; do not mark them working before then.
- Sunrise/sunset without lat/long: document the fallback (manual offset)
  honestly instead of silently using a default location.
- Letting the UI invent validation rules the firmware doesn't have — one
  validator source, mirrored in Zod.
