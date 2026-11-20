// Hardware mock data — mirrors mockup-ui §7.8 (KC868-AP reference board).
// Used as a fallback when the controller API is unreachable (mock mode,
// DESIGN §14.4) so the Hardware screens develop without hardware.
import type {
  BoardProfile,
  BoardPin,
  BoardCandidate,
  HardwareStatus,
  I2cScanResult,
} from "@/schemas";

export const mockBoardCandidates: BoardCandidate[] = [
  {
    id: "kincony_kc868_ap",
    name: "KinCony KC868-AP",
    mcu: "esp32",
    builtIn: true,
    desc: "Reference board. I2C verified 2026-11-19 (PCA9685 @0x40).",
    active: true,
  },
  {
    id: "generic_esp32",
    name: "Generic ESP32",
    mcu: "esp32",
    builtIn: true,
    desc: "Bare ESP32 dev module, no fixed peripherals.",
  },
  {
    id: "generic_esp32_pca9685",
    name: "Generic ESP32 + PCA9685",
    mcu: "esp32",
    builtIn: true,
    desc: "16-channel PWM driver over I2C.",
  },
  {
    id: "generic_esp32_pcf8574",
    name: "Generic ESP32 + PCF8574",
    mcu: "esp32",
    builtIn: true,
    desc: "8-bit I2C I/O expander inputs.",
  },
  {
    id: "generic_esp32_pwm",
    name: "Generic ESP32 PWM",
    mcu: "esp32",
    builtIn: true,
    desc: "Native LEDC PWM channels.",
  },
  {
    id: "generic_esp32s3",
    name: "Generic ESP32-S3",
    mcu: "esp32s3",
    builtIn: true,
    desc: "Bare ESP32-S3 dev module.",
  },
  {
    id: "custom_kc868_rmt",
    name: "KC868-AP + Cove RMT Custom",
    mcu: "esp32",
    builtIn: false,
    desc: "Custom variant mapping GPIO 25 to RMT WS2812B cove strip.",
  },
];

export const mockActiveBoard: BoardProfile = {
  schema_version: 1,
  id: "kincony_kc868_ap",
  name: "KinCony KC868-AP",
  manufacturer: "KinCony",
  mcu: "esp32",
  description: "Reference board, I2C verified 2026-11-19.",
  resources: [],
  peripherals: [
    { id: "pca9685_0", type: "pca9685", bus: "i2c0", address: 64, channels: 16, frequency: 1000 },
    { id: "pcf8574_di1_8", type: "pcf8574", bus: "i2c0", address: 34, channels: 8 },
    { id: "pcf8574_di9_16", type: "pcf8574", bus: "i2c0", address: 33, channels: 8 },
    { id: "relay1", type: "relay", bus: "gpio_relay1", channels: 1 },
    { id: "relay2", type: "relay", bus: "gpio_relay2", channels: 1 },
  ],
};

// Pin mapping grid — mockup gpioPins (KC868-AP). GPIO 26 shows a seeded
// conflict to demonstrate the inline validation rendering.
export const mockBoardPins: BoardPin[] = [
  { gpio: 0, role: "Download Button", dir: "input", strapping: true, conflict: false, input_only: false },
  { gpio: 2, role: "Relay 2 (Safe: OFF)", dir: "output", strapping: true, conflict: false },
  { gpio: 4, role: "I2C0 SDA", dir: "bidirectional", strapping: false, conflict: false },
  { gpio: 5, role: "Serial RX", dir: "input", strapping: true, conflict: false },
  { gpio: 13, role: "Relay 1 (Safe: OFF)", dir: "output", strapping: false, conflict: false },
  { gpio: 14, role: "RS485 TX", dir: "output", strapping: false, conflict: false },
  { gpio: 16, role: "I2C0 SCL", dir: "bidirectional", strapping: false, conflict: false },
  { gpio: 17, role: "ETH Clock Out", dir: "output", strapping: false, conflict: false },
  { gpio: 18, role: "ETH MDIO", dir: "bidirectional", strapping: false, conflict: false },
  { gpio: 23, role: "ETH MDC", dir: "output", strapping: false, conflict: false },
  { gpio: 25, role: "RMT0 Cove LEDs", dir: "output", strapping: false, conflict: false },
  { gpio: 32, role: "Serial TX", dir: "output", strapping: false, conflict: false },
  { gpio: 33, role: "RS485 RX", dir: "input", strapping: false, conflict: false },
  { gpio: 34, role: "Digital Input 17", dir: "input-only", strapping: false, conflict: false, input_only: true },
  { gpio: 35, role: "Digital Input 18", dir: "input-only", strapping: false, conflict: false, input_only: true },
  { gpio: 26, role: "Unassigned", dir: "free", strapping: false, conflict: true, reason: "Duplicate assignment: GPIO 26 also claimed by ledc_pwm0" },
];

export const mockHardwareStatus: HardwareStatus = {
  active_board: "kincony_kc868_ap",
  active_board_name: "KinCony KC868-AP",
  mcu: "esp32",
  pca9685_present: true,
  pcf8574_health: { pcf8574_di1_8: 0, pcf8574_di9_16: 0 },
  ethernet_link: false,
  free_heap: 240000,
  min_free_heap: 210000,
  outputs: [
    { id: "ch0", name: "PCA9685 CH0", driver: "pca9685_0", type: "pwm", state: false, value: 0 },
    { id: "ch4", name: "PCA9685 CH4", driver: "pca9685_0", type: "pwm", state: true, value: 82 },
    { id: "relay1", name: "Relay 1", driver: "relay1 (GPIO 13)", type: "relay", state: false },
    { id: "relay2", name: "Relay 2", driver: "relay2 (GPIO 2)", type: "relay", state: false },
  ],
  inputs: [
    { id: "in_17", name: "Digital Input 17", source: "GPIO 34 (input-only)", state: false, last_event: "Long press (yesterday)" },
    { id: "in_18", name: "Digital Input 18", source: "GPIO 35 (input-only)", state: true, last_event: "Closed (3h ago)" },
  ],
};

export const mockI2cScan: I2cScanResult = {
  bus: 0,
  sda_gpio: 4,
  scl_gpio: 16,
  devices: [
    { address: 33, address_hex: "0x21", known: true, label: "PCF8574 DI9-16", unexpected: false },
    { address: 34, address_hex: "0x22", known: true, label: "PCF8574 DI1-8", unexpected: false },
    { address: 64, address_hex: "0x40", known: true, label: "PCA9685", unexpected: false },
    { address: 112, address_hex: "0x70", known: false, label: "Unknown device", unexpected: true },
  ],
};