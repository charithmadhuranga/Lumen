# Hardware Profiles

Board profiles are versioned JSON in `boards/`, validated against
`schemas/board-profile.schema.json` and applied transactionally
(`PinValidator`, Phase 2). Rules: AGENTS.md §6.

## Reference board: KinCony KC868-AP

### Verified on physical hardware (esptool, 2026-11-18)

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

### Unverified on hardware (do not treat as fact)

Items from `controller-presets/kincony-AP-Controller/pinmap.txt` are
vendor reference only (read-only, never a source of verified facts):

- [ ] PCA9685 I2C address (preset suggests `0x00`; **scan first, report,
      require confirmation — do not assume `0x40` either**)
- [ ] PCA9685 enable/reset wiring
- [ ] Relay pin polarity (active high vs active low)
- [ ] PCF8574 presence and address
- [ ] Ethernet (LAN8720) clock pin and PHY address — KC868-AP variants differ
- [ ] RS485 DE/RE direction pin
- [ ] Input/button pin pull and polarity
- [ ] Onboard LED pin (if any)

These are confirmed only by physical tests in Phase 3 and are recorded in
`docs/IMPLEMENTATION_STATUS.md`'s hardware verification log.

## Profile files

Built-in profiles (versioned JSON in `boards/`), validated against
`schemas/board-profile.schema.json` and the PinValidator:

| Profile | MCU | Verified on hardware | Notes |
|---|---|---|---|
| `kincony_kc868_ap.json` | esp32 | No (firmware boots, peripherals unverified) | All addresses/polarities from vendor pinmap marked `unverified`; I2C scan in Phase 3 resolves PCA9685 (0x00 vs 0x40) and PCF8574 (0x22/0x21 vs 0x3A/0x21) |
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
