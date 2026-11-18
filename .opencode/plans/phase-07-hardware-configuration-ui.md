# Phase 7 — Hardware Configuration UI

**SPEC §26 Phase 7, SPEC §5.4, §6, §17.2 (hardware endpoints); DESIGN §7.8** ·
**Skills:** `lumen-webui`, `lumen-board-hal`

## Outcome

A user can select, duplicate, edit, validate and safely apply board profiles
from the browser — with graphical pin mapping, conflict indicators, I2C scan,
peripheral editing, import/export, and a rollback-visible apply workflow.

## Dependencies

Phase 2 (profiles, PinValidator, apply state machine), Phase 6 (web stack).

## Tasks

### 7.1 Firmware hardware endpoints (`src/web/api_routes.*`)

- [ ] From SPEC §17.2: `GET /boards`, `GET /boards/active`,
      `POST /boards/validate`, `POST /boards/apply`, `POST /boards`,
      `PUT /boards/{id}`, `DELETE /boards/{id}`, `GET /hardware/status`,
      `GET /hardware/i2c/scan`, `GET /hardware/inputs`.
- [ ] Built-in profiles read-only: edit attempts return a typed error;
      duplicate-first flow supported.
- [ ] `POST /boards/validate` returns the structured `ValidationReport`
      (pin id + reason code) — same shape the UI renders inline.
- [ ] `POST /boards/apply` runs the Phase-2 transactional state machine and
      streams or returns progress/result including **rollback outcome** on
      failure.
- [ ] Import/export: JSON in/out with schema validation; secrets never
      included (there are none in profiles, keep it that way).
- [ ] `GET /hardware/status`: live output/input state, peripheral health
      (PCA9685 present, PCF8574 health counters, Ethernet link), surfaced as
      `hardware_status_changed` WS events.

### 7.2 Web UI Hardware screens (DESIGN §7.8)

- [ ] Tab shell: **Board, Pin Mapping, Peripherals, Outputs, Inputs,
      Diagnostics** (lazy route).
- [ ] **Board:** active profile card; profile list with lock icon on
      built-ins; Duplicate / Import / Export / Restore-factory actions.
- [ ] **Pin Mapping:** graphical GPIO grid for the selected MCU; each pin a
      `PinChip` colored by role with conflict / strapping / input-only
      indicators; click to assign; `POST /boards/validate` errors appear
      inline next to the offending pin. All pin data comes from the API —
      zero GPIO knowledge compiled into the frontend (AGENTS.md §9).
- [ ] **Peripherals:** add/edit PCA9685, PCF8574, relay, Ethernet, RS485,
      addressable LED forms (RHF + Zod, mirroring the profile schema). I2C
      **Scan** button → `GET /hardware/i2c/scan`, highlights unexpected
      addresses; PCA9685 `address_verification_required` flow requires
      explicit user confirmation of a non-standard address (SPEC §6.3).
- [ ] **Outputs / Inputs:** tables with live state + test buttons
      (command → hardware, state back over WS).
- [ ] **Diagnostics:** free heap, min heap, task stats, I2C health, recent
      errors via Recharts (lazy-loaded, polled ≤1 Hz only while the tab is
      visible — DESIGN §8.4).
- [ ] **Apply workflow:** Validate → review summary → `alert-dialog`
      warning that outputs go to safe states → apply with progress → success
      or explicit rollback result (DESIGN §7.8).

### 7.3 Tests

- [ ] Firmware host tests: endpoint validation errors, apply success/rollback
      via fault injection, built-in profile protection.
- [ ] Web unit tests: `PinChip` conflict states, validation-error rendering,
      apply workflow state machine, import validation.
- [ ] Playwright: create custom profile → validate → conflict shown → fix →
      apply (mock mode), plus axe pass.

## Contracts & interfaces introduced

- Hardware endpoint set + `ValidationReport` schema (Zod ↔ firmware
  validator kept in lockstep).
- Apply-progress event stream (WS `hardware_status_changed` or REST poll —
  pick one and document it in `docs/API.md`).

## Gate (acceptance criteria)

1. User can create and save a custom board profile entirely from the UI.
2. Pin conflicts are shown with reasons, inline at the offending pin.
3. A failed apply rolls back and the previous profile remains active and
   visible (demonstrated in test + on hardware).
4. I2C scan reports actual devices; unexpected addresses are highlighted and
   confirmation is required for the PCA9685 address deviation.
5. No GPIO/pin logic outside the Hardware feature, and there only as API data.
6. `lint/typecheck/test/build` + axe green; Diagnostics chunk within budget.

## Docs to update

`docs/API.md` (hardware endpoints), `docs/HARDWARE_PROFILES.md` (UI workflow),
`docs/UI_UX.md`, `docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- MCU pin grid data (which GPIOs exist, strapping roles per MCU) must come
  from the API (profile/validator), not frontend constants — plan the
  `GET /boards/mcu-capabilities` style payload if one is missing.
- Apply progress streaming vs polling: decide before building the UI to
  avoid double implementations.
