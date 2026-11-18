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

- `boards/README.md` — placeholder until Phase 2/3 lands the first real
  profile (`boards/kincony_kc868_ap.json`, SPEC §6.3).

## MCU capability notes

- "MCU capability does not mean the board exposes it" (AGENTS.md §6): ESP32
  has 16 ADC channels, but only pins broken out and not strapped for other
  roles may be used — the profile lists what the *board* exposes.
