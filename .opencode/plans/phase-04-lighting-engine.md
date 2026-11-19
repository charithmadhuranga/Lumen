# Phase 4 — Lighting Engine

**SPEC §26 Phase 4, SPEC §8, §7.5** · **Skill:** `lumen-lighting-engine`

## Outcome

Logical lighting channels with normalized state, a color engine, non-blocking
transitions and a cancellable effects engine — all hardware-independent. All
seven logical types work: `switch`, `dimmer`, `rgb`, `rgbw`, `cct`,
`addressable_rgb`, `addressable_rgbw`.

## Dependencies

Phase 2 (drivers, I2C manager, PinValidator). Phase 3 recommended first for
the reference board.

## Tasks

### 4.1 Channel model (`src/lighting/channel_manager.*`)

- [x] Logical channel per SPEC §8.1 (id, name, type, room_id, driver
      reference `{type, device_id, channel}`, limits, transition_ms,
      startup_behavior).
- [x] Channels load from `/config/lights/channels.json`; a channel whose
      driver target is missing/invalid is reported as `unavailable` — the
      rest keep working (SPEC §1.2 rule 6).
- [x] State triple kept separate: **requested** (user intent), **applied**
      (what the engine last commanded), **actual** (read-back/feedback where
      available) — SPEC §8.3.
- [x] Type-aware validation layer: an unsupported property (e.g. color on a
      `dimmer`) is rejected before it reaches a driver.

### 4.2 Color engine (`src/lighting/color_engine.*`)

- [x] RGB↔HSV, RGBW handling, CCT (kelvin↔RGB mixing), gamma correction,
      brightness compensation, per-channel calibration and limits
      (SPEC §8.4).
- [x] Pure functions, no I/O — fully host-tested.
- [x] One code path for brightness/color applied across LEDC, PCA9685 and
      RMT outputs.

### 4.3 Command path and lighting task

- [x] Bounded lighting command queue; producers (REST/WS/MQTT/inputs/
      automation) enqueue only (AGENTS.md §4.7).
- [x] Lighting service processes commands in one task context; newer command
      replaces an active transition/effect for the same channel (SPEC §8.5).
- [x] Skip redundant hardware writes (same duty → no I2C transaction).
- [x] Startup behavior: `restore` last applied state or `off`/configured
      safe value per channel.

### 4.4 Transition engine (`src/lighting/transition_engine.*`)

- [x] Non-blocking interpolation (brightness, RGB, CCT, scene-wide),
      configurable duration, smooth easing, cancellation, replacement.
- [x] Timing driven by a tick/step schedule — **no delay loops in the
      lighting task** (AGENTS.md §5). ≥30 updates/s where hardware permits
      (SPEC §24).

### 4.5 Effects engine (`src/lighting/effects_engine.*`)

- [x] Effects 1–9 from SPEC §8.6: breathing, pulse, rainbow, color cycle,
      candlelight, sunrise, sunset, strobe, custom sequence.
- [x] Resource-managed (bounded effect instances), cancellable by any direct
      command or automation action, effect speed parameter.
- [x] Addressable LED strip support via RMT driver (SPEC §7.5): WS2812/
      WS2812B/SK6812, RGB/RGBW, LED count, color order, timing config —
      updates run independently of web/networking tasks.

### 4.6 Driver wiring

- [x] Channel → driver adapters: LEDC (dimmer/switch), PCA9685 (16-ch
      dimmer/RGB/RGBW/CCT groups), relay (switch), RMT (addressable).
- [x] Adapters resolve `device_id` through the hardware manager — no direct
      GPIO anywhere in `src/lighting/` (AGENTS.md §4.1).

## Contracts & interfaces introduced

- `LightState {power, brightness, r, g, b, w, color_temperature}` (SPEC §8.3).
- Command envelope `LIGHT_COMMAND {channel_id, state_patch, transition_ms,
  origin, command_id}` — origin participates in Phase 11 conflict rules.
- Bus events: `LIGHT_STATE_CHANGED` published **after** hardware application.
- Public engine API used by REST/MQTT/automation: `set_state`, `command`,
  `get_state`.

## Gate (acceptance criteria)

1. All seven logical types respond correctly on hardware (or mock, clearly ✅
   labeled) — dim, color, CCT, relay, addressable.
2. Commands are hardware-independent: the same test suite runs against LEDC ✅
   and PCA9685 mocks.
3. Transitions are non-blocking: lighting task never blocks > a few ms; no ✅
   `vTaskDelay` inside a fade step loop (code review + timing test).
4. A newer command cancels an in-flight transition or effect (test). ✅
5. Unsupported-property commands are rejected with a typed error (test). ✅
6. Redundant-write suppression proven (mock I2C transaction counter). ✅
7. Host tests: color conversion golden values, gamma/limits, transition ✅
   interpolation, effect cancellation, requested vs applied state.
8. Heap impact reviewed; no dynamic allocation per update in the hot path. ✅

## Docs to update

`docs/ARCHITECTURE.md` (lighting engine + task), `docs/API.md` (light
command/state payloads — Zod schema added in Phase 6 alongside),
`docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- PCA9685 batch flush rate vs transition rate: tune one coalescing window so
  smooth fades do not flood the bus.
- Addressable LED timings verified against real strips in Phase 3-style
  hardware tests; keep color-order configurable rather than assuming GRB.
