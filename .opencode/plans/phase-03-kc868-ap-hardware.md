# Phase 3 — KC868-AP Reference Hardware Validation

**SPEC §26 Phase 3, SPEC §6.3, §25.2, §25.7** · **Skill:** `lumen-board-hal`

## Outcome

Every KC868-AP resource physically exercised and recorded: 16 PWM outputs,
16 expanded inputs, 2 direct inputs, 2 relays, Ethernet, RS485, download
button. The profile's `unverified` flags are cleared **only** where a test
passed, and `docs/HARDWARE_PROFILES.md` states the measured truth.

## Dependencies

Phase 2 (drivers + profile). Requires physical KC868-AP hardware and
low-voltage test loads.

> **Mock tests do not close this phase.** If hardware is unavailable, the
> phase stays open with everything marked "mock only" (SPEC §29 Rule 5).

## Tasks

### 3.1 Bring-up diagnostics

- [ ] I2C scan service result exposed (runs at boot when the profile flags
      `address_verification_required`): report every responding address with
      expected-vs-actual comparison.
- [ ] A small on-device diagnostic mode (build flag or serial command) that
      exercises one resource at a time under human observation: set PWM
      channel N to 25/50/75 %, toggle relay N, read input N, loopback RS485.
- [ ] Safe-first rule: run all of §3.2–3.5 before connecting any real load
      (SPEC §25.7).

### 3.2 PWM and relays (SPEC §25.2, §25.7)

- [ ] Verify all 16 PCA9685 outputs: address, frequency (profile default
      1000 Hz), duty→brightness linearity, output polarity, channel mapping.
- [ ] Verify both relays: GPIO polarity, boot/safe state (off), no glitch at
      init.
- [ ] Record pass/fail per channel in `docs/HARDWARE_PROFILES.md`.

### 3.3 Inputs

- [ ] Both PCF8574 devices: confirm actual I2C addresses (0x22/0x21 per
      pinmap — resolve against ESPHome's 0x3A/0x21), active polarity, all 16
      channels, debounce behavior, health (unplug/bus-error detection).
- [ ] Direct inputs GPIO34/GPIO35 (input-only): read state, confirm no
      internal pull behavior is assumed.
- [ ] Download button GPIO0: confirm strapping-safe read (never configured
      as output).

### 3.4 Ethernet and RS485

- [ ] Ethernet LAN8720: link up with profile settings (MDC 23, MDIO 18,
      clock GPIO17_OUT, PHY addr 0, power -1), DHCP + ping, sustained
      transfer. Verify clock mode against the schematic before trusting it.
- [ ] RS485: TX/RX loopback or terminal test at 115200 8N1; confirm
      direction-control behavior of the transceiver (auto vs manual DE/RE)
      and record it — Modbus transport in Phase 10 depends on this.

### 3.5 System-level hardware tests

- [ ] Extended serial (GPIO5/GPIO32) sanity check.
- [ ] Power-failure recovery: outputs return to configured startup behavior,
      config intact (SPEC §25.7 step 8).
- [ ] Thermal/current observation under rated test load; document limits.
- [ ] Fill in `test/hardware/kc868_ap/` with a repeatable test checklist
      script (results recorded manually per run).

## Gate (acceptance criteria)

1. Every resource in the §25.2 KC868-AP list has a physical pass/fail record.
2. Measured PCA9685 address, PCF8574 addresses and all polarities are written
   into `boards/kincony_kc868_ap.json` and `docs/HARDWARE_PROFILES.md`;
   `unverified` flags cleared only for proven items.
3. I2C diagnostics report the real device inventory.
4. Ethernet and RS485 outcomes documented (including any failure — a failed
   Ethernet init must degrade gracefully, not crash the boot).
5. No mains-connected load was used during validation.

## Docs to update

`boards/kincony_kc868_ap.json` (verified values + flags),
`docs/HARDWARE_PROFILES.md` (verification matrix with dates),
`docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- The `0x00` PCA9685 address in `pinmap.txt` is unusual; if nothing responds,
  scan wide and report candidates to the user for confirmation — do not
  auto-substitute `0x40` (AGENTS.md §6).
- RS485 direction control may need a GPIO that the pinmap does not list;
  escalate to the user rather than inventing a pin.
