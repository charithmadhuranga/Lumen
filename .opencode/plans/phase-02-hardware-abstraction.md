# Phase 2 — Hardware Abstraction Layer

**SPEC §26 Phase 2, SPEC §5, §6, §7 (P0 drivers)** · **Skill:** `lumen-board-hal`

## Outcome

Hardware-independent drivers behind HAL interfaces, a board-profile system
with schema + semantic validation, a `PinValidator` that catches conflicts,
and a transactional profile-apply workflow. Generic ESP32 works; the
KC868-AP profile loads; pin conflicts are detected; peripheral diagnostics
report honestly.

## Dependencies

Phase 1 (config manager, event bus).

## Tasks

### 2.1 HAL interfaces (`src/hardware/hal/`)

- [ ] Interfaces from SPEC §5.1: `IOutputDriver`, `IDigitalInputDriver`,
      `IPwmDriver`, plus `II2CBus`, `IUartDriver`, `IEthernetDriver`,
      `IAddressableLedDriver`, `IRelayDriver`, `IPeripheralDriver`.
- [ ] All methods return `esp_err_t`; init/shutdown where applicable; range
      validation in the driver, not the caller.
- [ ] No board knowledge in any interface header.

### 2.2 Board profile loader and PinValidator

- [ ] `board_profile.*`: parse + meta-schema validate
      `schemas/board-profile.schema.json` (SPEC §6.2).
- [ ] `pin_validator.*`: semantic validation (SPEC §5.3) — GPIO existence per
      MCU, input/output and input-only rules, strapping pins (0/2/12/15 on
      original ESP32), flash/PSRAM reserved pins, duplicate assignments,
      UART/I2C/Ethernet conflicts, native PWM channel limits, RMT resource
      limits, peripheral references resolve to declared resources,
      board-specific reserved pins. MCU capability tables are per-MCU data,
      not `#ifdef` soup.
- [ ] Validation returns a structured error list (pin id + reason code) that
      the Phase-7 UI renders inline.
- [ ] Transactional runtime apply workflow (SPEC §5.4): validate → save
      candidate → stop affected services → safe states → init new hardware →
      run diagnostics → commit, or restore previous profile on any failure.
      A failed apply never destroys the last working configuration.

### 2.3 P0 drivers (SPEC §7.1)

- [ ] `gpio_driver` — digital in/out through HAL, inversion, safe states.
- [ ] `ledc_driver` — native PWM: timer frequency, resolution, channel
      allocation, duty, inversion, conflict detection, brightness curve hook.
- [ ] `i2c_manager` — single owner of the I2C bus(s): bus init from profile,
      transaction API with timeout/retry, error-recovery (bus reset),
      scan endpoint for diagnostics.
- [ ] `pca9685_driver` — 16 ch, 12-bit, configurable frequency/polarity,
      **batched pending updates** (the PWM service flushes coalesced writes;
      individual lights never touch I2C — AGENTS.md §6), I2C error recovery,
      output init to safe state, frequency validation.
- [ ] `pcf8574_driver` — 8 inputs/device, multiple devices, configurable
      polarity, debouncing, change detection → input events on the bus,
      device health monitoring (read failure counter).
- [ ] `relay_driver` — on/off with configured polarity and safe state.
- [ ] `input_manager` lives in `src/services/` and aggregates GPIO + PCF8574
      inputs into debounced logical input events (press/double/long handled
      in Phase 9's trigger layer; here: raw + debounced state only).

### 2.4 Mock drivers for host tests

- [ ] `test/unit/mocks/`: in-memory implementations of every HAL interface,
      an I2C bus simulator with programmable device responses (including
      error injection), and a fake PCA9685/PCF8574 register model.

### 2.5 Built-in board profiles (`boards/`)

- [ ] `kincony_kc868_ap.json` from SPEC §6.3, reconciled with
      `controller-presets/kincony-AP-Controller/pinmap.txt`
      (I2C SDA=4 SCL=16, PCF8574 0x22/0x21, DI17/18 = GPIO34/35,
      relays GPIO13/GPIO2, ETH MDC=23 MDIO=18 CLK=GPIO17_OUT, RS485 RX=33
      TX=14, extend serial 5/32, download button GPIO0).
      **All addresses/polarities marked `unverified: true` until Phase 3
      measures them.**
- [ ] `generic_esp32.json`, `generic_esp32s3.json`,
      `generic_esp32_pca9685.json`, `generic_esp32_pcf8574.json`,
      `generic_esp32_pwm.json`.
- [ ] Built-in profiles read-only at runtime; users duplicate them
      (SPEC §6.1).

## Contracts & interfaces introduced

- HAL interface set (frozen unless a real driver forces a change).
- `ValidationReport { errors[], warnings[] }` with stable reason codes.
- Profile-apply state machine consumed later by `POST /boards/apply`.
- Input events on the bus: `INPUT_STATE_CHANGED {input_id, level, timestamp}`.

## Gate (acceptance criteria)

1. `pio run -e esp32` / `esp32s3` green; host tests green.
2. Generic ESP32 profile: native PWM + GPIO + inputs verified on a real
   generic board (or explicitly marked "mock only" if no board is available).
3. KC868-AP profile loads and passes schema + semantic validation.
4. Conflict detection tests: duplicate GPIO, strapping misuse, input-only as
   output, UART clash, PWM channel overflow, unresolved peripheral reference
   — each yields the correct structured error (dedicated test suite,
   AGENTS.md §11).
5. Transactional apply: failure at step 5/6 restores previous profile and
   leaves config intact (host test with fault injection).
6. PCA9685 batching proven by test: N light writes → ≤ expected I2C
   transactions; redundant writes skipped.
7. PCF8574 debounce + change detection proven in the bus simulator.

## Docs to update

`docs/ARCHITECTURE.md` (HAL + driver overview, task registry),
`docs/HARDWARE_PROFILES.md` (each profile, verification status column),
`docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- PCA9685 address on KC868-AP: `pinmap.txt` says `0x00`, vendor examples use
  `0x40`. Ship the profile with the user-supplied value + an
  `address_verification_required` flag; the I2C scan in Phase 3 resolves it.
  Never silently rewrite (AGENTS.md §6).
- PCF8574 addresses conflict between sources (`0x22/0x21` vs `0x3A/0x21` in
  the ESPHome example) and inputs are active-low — same treatment.
- Do not claim Ethernet clock mode or RS485 direction behavior correct before
  physical verification.
