# Hardware Profiles

Board profiles are versioned JSON in `boards/`, validated against
`schemas/board-profile.schema.json` and applied transactionally
(`PinValidator`, Phase 2). Rules: AGENTS.md §6.

## Reference board: KinCony KC868-AP

### Verified on physical hardware (esptool, 2026-11-18 to 2026-11-19)

Probed on the attached device at `/dev/ttyUSB1` (CH341 USB-serial):

| Fact | Value |
|---|---|
| Chip | ESP32-D0WD-V3, revision v3.0 |
| Cores / clock | 2 × 240 MHz capable (boot banner shows configured CPU freq) |
| MAC | `40:22:d8:67:07:98` (KinCony OUI) |
| Flash | 4 MB (manufacturer `5e`, device `4016`) |
| Crystal | 40 MHz |
| USB bridge | CH341 (`1a86:7523`) on `/dev/ttyUSB1` |
| Bootloader accepts partition table | yes (dual OTA + LittleFS, 2026-11-18) |
| Firmware boots + heartbeats | yes (2026-11-18) |
| I2C bus scan (SDA=4, SCL=16) | Devices at `0x21`, `0x22`, `0x40`, `0x70`, probe at `0x00` |

### I2C peripheral addresses — VERIFIED by bus scan (2026-11-19)

| Device | Pinmap claimed | Scan found | Status |
|---|---|---|---|
| PCF8574 DI1-8 | `0x22` | `0x22` | ✅ Verified — matches pinmap |
| PCF8574 DI9-16 | `0x21` | `0x21` | ✅ Verified — matches pinmap |
| PCA9685 | `0x00` | `0x40` | ✅ Verified — **pinmap was wrong**, vendor example `0x40` is correct |
| Unknown device | — | `0x70` | ⚠️ Not in pinmap, purpose unknown |
| General call | `0x00` | probe responded | ⚠️ Likely general-call response, not a device at `0x00` |

### Peripheral functional tests — VERIFIED (2026-11-19)

| Resource | Test | Result |
|---|---|---|
| PCA9685 0x40 | All 16 PWM channels written at 25% duty, then returned to off | ✅ 16/16 channels OK |
| PCF8574 0x21 | Input register read | ✅ Readable (raw=0x00) |
| PCF8574 0x22 | Input register read | ✅ Readable (raw=0x00) |
| Relay1 GPIO13 | Toggle on/off | ✅ GPIO toggles |
| Relay2 GPIO2 | Toggle on/off | ✅ GPIO toggles |
| GPIO34 (DI17) | Input read | ✅ Reads as 1 |
| GPIO35 (DI18) | Input read | ✅ Reads as 1 |
| GPIO0 (download button) | Input read | ✅ Reads as 1 |

### Still unverified on hardware

- [ ] PCA9685 output polarity (active-high vs active-low on PWM outputs)
- [ ] Relay polarity (active-high vs active-low at the relay contacts)
- [ ] PCF8574 active polarity (pinmap says active-low; inputs all read 0, can't determine polarity without driving inputs)
- [ ] Ethernet (LAN8720) clock pin and PHY address — KC868-AP variants differ
- [ ] RS485 DE/RE direction pin
- [ ] Onboard LED pin (if any)

These are confirmed only by physical tests and are recorded in `docs/IMPLEMENTATION_STATUS.md`'s hardware verification log.

## Profile files

Built-in profiles (versioned JSON in `boards/`), validated against
`schemas/board-profile.schema.json` and the PinValidator:

| Profile | MCU | Verified on hardware | Notes |
|---|---|---|---|
| `kincony_kc868_ap.json` | esp32 | Partial (I2C addresses + PCA9685 channels + relay GPIOs + PCF8574 + GPIO inputs verified; relay polarity, Ethernet, RS485 unverified) | I2C scan + functional diagnostics on 2026-11-19. See verification tables above. |
| `generic_esp32.json` | esp32 | No | Marked "mock only" until tested on a real generic board |
| `generic_esp32s3.json` | esp32s3 | Compile-only | No S3 hardware available for physical test |
| `generic_esp32_pca9685.json` | esp32 | No | PCA9685 address (0x40 default) unverified |
| `generic_esp32_pcf8574.json` | esp32 | No | PCF8574 address (0x20 default) unverified |
| `generic_esp32_pwm.json` | esp32 | No | Marked "mock only" until tested on a real board |

Built-in profiles are read-only at runtime; users duplicate them to create
custom variants (SPEC §6.1).

- `boards/README.md` — placeholder note (see above).

## MCU capability notes

- "MCU capability does not mean the board exposes it" (AGENTS.md §6): ESP32
  has 16 ADC channels, but only pins broken out and not strapped for other
  roles may be used — the profile lists what the *board* exposes.
