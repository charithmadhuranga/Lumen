---
name: lumen-lighting-engine
description: Lighting engine work for Lumen — logical channels, normalized LightState (requested/applied/actual), type-aware validation, color engine (RGB/HSV/RGBW/CCT/gamma), non-blocking transitions, effects, plus rooms, groups with aggregation policies, and scenes. Use when implementing or changing anything under src/lighting/, light commands, group/room/scene behavior, or the REST payloads for them. Triggers: "lighting engine", "light state", "transition", "effects", "color engine", "scene", "group aggregation", "room", "phase 4", "phase 5".
---

# ESP Lighting — Lighting Engine

**Plans:** `.opencode/plans/phase-04-lighting-engine.md`,
`phase-05-rooms-groups-scenes.md`.
**Spec:** SPEC §8 (engine), §9 (rooms/groups/scenes).

## Model

- Every light is a **logical channel** (SPEC §8.1): id, name, type
  (`switch|dimmer|rgb|rgbw|cct|addressable_rgb|addressable_rgbw`), room,
  driver reference `{type, device_id, channel}`, limits, `transition_ms`,
  `startup_behavior`. Channels resolve drivers through the hardware manager —
  `src/lighting/` contains zero GPIO/pin knowledge.
- State triple kept separate: **requested** (intent) → **applied** (last
  commanded) → **actual** (read-back where available). Publish
  `LIGHT_STATE_CHANGED` on the bus only after hardware application.
- Type-aware validation rejects unsupported properties (color on a `dimmer`)
  before any driver sees them.

## Rules

- One normalized `LightState {power, brightness, r, g, b, w,
  color_temperature}` for all driver types.
- **Color engine only** for conversions (RGB/HSV/RGBW/CCT, gamma, brightness
  compensation, calibration, limits) — no ad-hoc math elsewhere. Pure
  functions, host-tested golden values.
- Transitions and effects are non-blocking and cancellable; **a newer command
  replaces an active transition or effect** for that channel. No delay loops
  in the lighting task (AGENTS.md §7); ≥30 updates/s where hardware permits.
- Producers (REST/WS/MQTT/inputs/automation) enqueue into a bounded command
  queue; the lighting service is the only executor.
- Skip redundant hardware writes (same duty → no I2C transaction).
- Effects (breathing, pulse, rainbow, color cycle, candlelight, sunrise,
  sunset, strobe, custom sequence) are resource-managed instances, cancelled
  by any direct command.

## Rooms, groups, scenes (Phase 5)

- Room ops fan out through the engine; deleting a room/group/light never
  cascades destructively (orphans handled).
- **Group aggregation policy** (documented + tested): brightness adjusts all
  members while preserving each member's color; RGB/CCT apply only to
  supporting members; on/off applies to all; group state is an aggregate for
  the UI.
- Scenes (SPEC §9.3 JSON): save current state, edit, activate with the
  scene's `transition_ms`, duplicate, export/import with validation.
  Activation = one coordinated command batch sharing a `command_id`
  (matters for Phase 11 conflict rules) and publishes `SCENE_ACTIVATED`
  (an automation trigger).
- All documents go through the transactional config store with
  `schema_version`.

## Validation gate

- [ ] All seven light types work (hardware or clearly-labeled mock).
- [ ] Same test suite passes against LEDC and PCA9685 mocks (hardware
      independence).
- [ ] Newer command cancels in-flight transition/effect (test).
- [ ] Unsupported-property command → typed rejection (test).
- [ ] Redundant-write suppression proven (mock I2C counter).
- [ ] No blocking in the transition step path (review + timing test).
- [ ] Host tests: color golden values, gamma/limits, interpolation,
      effect cancellation, scene round-trip, aggregation matrix
      (dimmer-only / RGB-only / mixed groups).
- [ ] No per-update heap allocation in the hot path.

## Pitfalls

- Applying brightness as a raw multiplier that clips colors (use the color
  engine's compensation).
- Group commands fanning out without a shared `command_id` — breaks Phase 11
  dedup.
- Persisting applied state per frame to flash (wear) — persist config and
  debounced state only.
- Letting scene save persist unknown light ids ("partial" broken scenes).
