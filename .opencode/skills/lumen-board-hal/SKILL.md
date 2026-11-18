---
name: lumen-board-hal
description: Hardware abstraction work for Lumen — HAL interfaces, board profiles (JSON), PinValidator, transactional profile apply, and drivers (GPIO, LEDC PWM, I2C manager, PCA9685, PCF8574, relay, RMT, Ethernet, RS485), plus KC868-AP physical bring-up and hardware verification. Use when implementing drivers, adding/editing board profiles, validating pins, applying profiles at runtime, or verifying hardware. Triggers: "driver", "pca9685", "pcf8574", "board profile", "pin validator", "gpio", "ledc", "i2c", "kc868", "hardware bring-up", "phase 2", "phase 3".
---

# ESP Lighting — Hardware Abstraction & Drivers

**Plans:** `.opencode/plans/phase-02-hardware-abstraction.md`,
`phase-03-kc868-ap-hardware.md`.
**Spec:** SPEC §5 (HAL), §6 (profiles), §7 (drivers), §25.2/§25.7 (hardware
tests).

## Hard rules

- **No GPIO numbers outside `boards/*.json`.** Application modules resolve
  everything through the hardware manager (AGENTS.md §4.1).
- One pin never belongs to two active peripherals; the `PinValidator`
  decides, humans don't eyeball it (AGENTS.md §4.4).
- MCU capability ≠ board availability. Declare capabilities honestly.
- The single I2C owner is `i2c_manager`; lighting channels never write I2C
  directly — PCA9685 batches (AGENTS.md §6).
- **Unverified stays unverified.** KC868-AP: PCA9685 address `0x00`
  (pinmap) vs `0x40` (vendor examples) — scan, report, require
  confirmation; PCF8574 addresses (`0x22/0x21` vs `0x3A/0x21`) and input/
  relay polarity, Ethernet clock mode, RS485 direction — all physical-test
  only. Never "fix" a user-supplied address to the standard one.

## Procedure — new driver

1. Pick the HAL interface (`IOutputDriver`, `IPwmDriver`,
   `IDigitalInputDriver`, `II2CBus`, `IUartDriver`, `IEthernetDriver`,
   `IAddressableLedDriver`, `IRelayDriver`, `IPeripheralDriver`); do not
   widen it without need.
2. Implement `initialize`/`shutdown` where applicable, range validation
   inside the driver, every error as `esp_err_t` — never ignored.
3. Register claimed resources with the pin/resource validator so conflicts
   are caught at profile-apply time.
4. Write the host mock in `test/unit/mocks/` (I2C bus simulator with error
   injection) and unit tests in the same change.
5. Hardware-facing behavior (address, polarity, timing) gets a checklist
   entry in `test/hardware/` — mock tests never mark it verified.

## Procedure — board profile

1. JSON must conform to `schemas/board-profile.schema.json` (SPEC §6.2).
2. Semantic validation: GPIO exists per MCU, direction correct, no input-only
   misuse, strapping (0/2/12/15 original ESP32) and flash/PSRAM pins
   justified, no duplicates, UART/I2C/Ethernet/PWM/RMT conflicts, peripheral
   references resolve, board-reserved pins honored.
3. Mark unverified items in `settings` (`unverified: true`,
   `address_verification_required` where relevant) and in
   `docs/HARDWARE_PROFILES.md`.
4. Built-in profiles are read-only at runtime; users duplicate them.
5. Run `npm run validate:schemas`, then the PinValidator suite.

## Procedure — apply a profile at runtime (SPEC §5.4)

validate candidate → save temporary candidate → stop affected services →
outputs to configured safe states → init new hardware → run peripheral
diagnostics → commit; on **any** failure restore the previous profile. A
failed apply must never destroy the last working configuration.

## KC868-AP reference facts (from `controller-presets/…/pinmap.txt`)

I2C SDA=4 SCL=16 · PCF8574 0x22 (DI1–8), 0x21 (DI9–16), active-low ·
PCA9685 0x00 (unverified!) · DI17/18 = GPIO34/35 · Relay1=13, Relay2=2 ·
Button=GPIO0 · ETH: LAN8720, addr 0, MDC 23, MDIO 18, CLK GPIO17_OUT,
power -1 · RS485 RX=33 TX=14 · extend serial 5/32.

## Validation gate (Phases 2–3)

- [ ] `pio run -e esp32` / `esp32s3` + `pio test -e native` green.
- [ ] Conflict suite: duplicate GPIO, strapping misuse, input-only as output,
      UART clash, PWM overflow, unresolved reference — each correct error.
- [ ] Profile apply success + fault-injected rollback (config intact).
- [ ] PCA9685 batching proven (transaction counter) and redundant writes
      skipped.
- [ ] PCF8574 debounce/change/health tested in the bus simulator.
- [ ] Phase 3 only: physical pass/fail recorded per resource in
      `docs/HARDWARE_PROFILES.md`; `unverified` flags cleared only where
      proven; mains loads never used during validation.

## Pitfalls

- Claiming a driver works because it compiles — it's "mock only" until it
  talked to the chip.
- Initializing PCA9685 outputs to full-on instead of the safe state.
- Blocking I2C transactions in a high-priority task (SPEC §24).
- Assuming the ESPHome/Arduino example pinmap is correct for this board.
