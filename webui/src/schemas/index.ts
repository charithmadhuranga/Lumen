// Zod schemas — the API contract with the firmware (SPEC §17, DESIGN §8.1).
// Every REST response and WebSocket message is parsed with these; types are
// z.infer'd, never hand-written. Keep in lockstep with src/web/api_*.cpp.
import { z } from "zod";

/* ------------------------------------------------------------------ auth */
export const SessionInfo = z.object({
  username: z.string(),
  forcePasswordChange: z.boolean(),
  csrfToken: z.string(),
});

export const PasswordChangeForm = z
  .object({
    currentPassword: z.string().min(1, "Enter your current password"),
    newPassword: z
      .string()
      .min(10, "Use at least 10 characters")
      .max(128, "Use at most 128 characters")
      .refine((v) => v.toLowerCase() !== "admin", "Choose something other than the default"),
    confirmPassword: z.string(),
  })
  .refine((v) => v.newPassword === v.confirmPassword, {
    message: "Passwords do not match",
    path: ["confirmPassword"],
  });

/* ---------------------------------------------------------------- system */
export const SystemInfo = z.object({
  firmwareVersion: z.string(),
  buildDate: z.string().optional(),
  idfVersion: z.string(),
  chipModel: z.string(),
  chipRevision: z.number(),
  cores: z.number(),
  cpuFreqMHz: z.number(),
  flashSize: z.number(),
  freeHeap: z.number(),
  minFreeHeap: z.number(),
  largestFreeBlock: z.number().optional(),
  uptimeSec: z.number(),
  resetReason: z.string(),
  fsTotal: z.number(),
  fsUsed: z.number(),
  partition: z.string(),
  hostname: z.string(),
  mac: z.string(),
  board: z.string(),
});

export const Health = z.object({
  ok: z.boolean(),
  version: z.string(),
  services: z.record(z.string(), z.boolean()),
});

/* ---------------------------------------------------------------- lights */
export const LightType = z.enum([
  "switch",
  "dimmer",
  "rgb",
  "rgbw",
  "cct",
  "addressable_rgb",
  "addressable_rgbw",
]);

export const DriverType = z.enum(["pca9685", "ledc", "relay", "gpio", "rmt"]);

export const LightState = z.object({
  power: z.boolean(),
  brightness: z.number().int().min(0).max(100),
  red: z.number().int().min(0).max(255),
  green: z.number().int().min(0).max(255),
  blue: z.number().int().min(0).max(255),
  white: z.number().int().min(0).max(255),
  colorTemperature: z.number().int().min(0).max(20000),
});

export const Capabilities = z.object({
  brightness: z.boolean(),
  color: z.boolean(),
  white: z.boolean(),
  colorTemperature: z.boolean(),
  effects: z.boolean(),
});

export const LightDriver = z.object({
  type: DriverType,
  deviceId: z.string(),
  channel: z.number().int().min(0).max(63),
});

export const LightLimits = z.object({
  minBrightness: z.number().int().min(0).max(100),
  maxBrightness: z.number().int().min(1).max(100),
  colorTemperatureMin: z.number().int(),
  colorTemperatureMax: z.number().int(),
});

export const Light = z.object({
  id: z.string(),
  name: z.string(),
  type: LightType,
  roomId: z.string(),
  driver: LightDriver,
  transitionMs: z.number().int().min(0),
  startup: z.enum(["restore", "on", "off"]),
  limits: LightLimits,
  calibration: z.object({ red: z.number(), green: z.number(), blue: z.number(), white: z.number() }),
  state: LightState, // target the user asked for
  output: LightState, // what the hardware shows right now
  available: z.boolean(),
  effect: z.string().optional(),
  capabilities: Capabilities.optional(),
});

export const LightCreate = z.object({
  id: z
    .string()
    .min(1, "Required")
    .max(48)
    .regex(/^[a-z0-9_-]+$/, "Use lowercase letters, digits, '_' or '-'"),
  name: z.string().min(1, "Required").max(64),
  type: LightType,
  roomId: z.string(),
  driver: LightDriver,
  transitionMs: z.number().int().min(0).max(600000),
  startup: z.enum(["restore", "on", "off"]),
});

export const LightCommand = z.object({
  power: z.boolean().optional(),
  toggle: z.boolean().optional(),
  brightness: z.number().int().min(0).max(100).optional(),
  red: z.number().int().min(0).max(255).optional(),
  green: z.number().int().min(0).max(255).optional(),
  blue: z.number().int().min(0).max(255).optional(),
  white: z.number().int().min(0).max(255).optional(),
  colorTemperature: z.number().int().min(1000).max(20000).optional(),
  transitionMs: z.number().int().min(0).max(600000).optional(),
  effect: z.string().optional(),
  effectParams: z.object({ speedMs: z.number().optional(), durationMs: z.number().optional() }).optional(),
  commandId: z.string().optional(),
});

export const Accepted = z.object({
  accepted: z.boolean(),
  commandId: z.string().optional(),
  lights: z.number().optional(),
});

/* ----------------------------------------------------------- rooms/groups */
export const Aggregate = z.object({
  on: z.boolean(),
  onCount: z.number(),
  total: z.number(),
  brightness: z.number(),
});

export const Room = z.object({
  id: z.string(),
  name: z.string(),
  floor: z.string(),
  icon: z.string(),
  floorplanId: z.string(),
  channelIds: z.array(z.string()),
  groupIds: z.array(z.string()),
  state: Aggregate,
});

export const RoomForm = z.object({
  name: z.string().min(1, "Required").max(64),
  floor: z.string().max(32),
  icon: z.string().max(32),
  channelIds: z.array(z.string()),
});

export const AggregationPolicy = z.enum(["average", "max", "min"]);

export const Group = z.object({
  id: z.string(),
  name: z.string(),
  roomId: z.string(),
  channelIds: z.array(z.string()),
  policy: AggregationPolicy,
  state: Aggregate,
});

/* ---------------------------------------------------------------- scenes */
export const SceneLightState = z.object({
  lightId: z.string(),
  power: z.boolean(),
  brightness: z.number().int().min(0).max(100),
  red: z.number().int().min(0).max(255),
  green: z.number().int().min(0).max(255),
  blue: z.number().int().min(0).max(255),
  white: z.number().int().min(0).max(255),
  colorTemperature: z.number().int().min(0).max(20000),
});

export const Scene = z.object({
  id: z.string(),
  name: z.string(),
  transitionMs: z.number().int().min(0),
  states: z.array(SceneLightState),
});

/* ------------------------------------------------------- boards/hardware */
export const BoardCandidate = z.object({
  id: z.string(),
  name: z.string(),
  mcu: z.string(),
  builtIn: z.boolean(),
  desc: z.string(),
  active: z.boolean(),
  unverified: z.boolean(),
});

export const BoardResource = z.object({
  id: z.string(),
  type: z.enum(["gpio", "pwm", "i2c", "uart", "rmt", "ethernet"]),
  gpio: z.number().int().optional(),
  direction: z.string().optional(),
  inverted: z.boolean().optional(),
  pull: z.string().optional(),
  safe_state: z.boolean().optional(),
  port: z.number().int().optional(),
  frequency: z.number().int().optional(),
  pins: z.record(z.string(), z.number().int()).optional(),
});

export const BoardPeripheral = z.object({
  id: z.string(),
  type: z.enum(["pca9685", "pcf8574", "relay", "ethernet", "rs485", "addressable_led"]),
  bus: z.string().optional(),
  address: z.number().int().optional(),
  channels: z.number().int().optional(),
  frequency: z.number().int().optional(),
  unverified: z.boolean().optional(),
  address_verification_required: z.boolean().optional(),
  unverified_note: z.string().optional(),
  active_low: z.boolean().optional(),
  settings: z.record(z.string(), z.unknown()).optional(),
});

export const BoardProfile = z.object({
  schema_version: z.number().int().optional(),
  id: z.string(),
  name: z.string(),
  manufacturer: z.string().optional(),
  mcu: z.string(),
  description: z.string().optional(),
  resources: z.array(BoardResource),
  peripherals: z.array(BoardPeripheral),
  unverified: z.boolean().optional(),
  builtIn: z.boolean().optional(),
  active: z.boolean().optional(),
});

export const PinInfo = z.object({
  gpio: z.number().int(),
  role: z.string(),
  owner: z.string(),
  dir: z.string(),
  strapping: z.boolean(),
  input_only: z.boolean(),
  reserved: z.boolean(),
  conflict: z.boolean(),
  reason: z.string().optional(),
});

export const ValidationEntry = z.object({
  code: z.string(),
  id: z.string().optional(),
  message: z.string(),
  is_error: z.boolean(),
});

export const ValidationReport = z.object({
  errors: z.array(ValidationEntry),
  warnings: z.array(ValidationEntry),
  has_errors: z.boolean(),
  error_count: z.number(),
  warning_count: z.number(),
  pins: z.array(PinInfo).optional(),
});

export const ApplyResult = z.object({
  applied: z.boolean(),
  rolled_back: z.boolean(),
  active_board: z.string(),
  message: z.string(),
  report: ValidationReport.optional(),
});

export const HardwareOutput = z.object({
  id: z.string(),
  name: z.string(),
  driver: z.string(),
  type: z.string(),
  state: z.boolean(),
  value: z.number(),
  lightId: z.string().optional(),
  lightName: z.string().optional(),
});

export const HardwareInput = z.object({
  id: z.string(),
  name: z.string(),
  source: z.string(),
  state: z.boolean(),
  healthy: z.boolean(),
  changedMs: z.number(),
});

export const PeripheralHealth = z.object({
  id: z.string(),
  type: z.string(),
  ok: z.boolean(),
  errors: z.number(),
  detail: z.string(),
});

export const HardwareStatus = z.object({
  active_board: z.string(),
  active_board_name: z.string(),
  mcu: z.string(),
  outputs: z.array(HardwareOutput),
  inputs: z.array(HardwareInput),
  peripherals: z.array(PeripheralHealth),
  pca9685_present: z.boolean(),
  ethernet_link: z.boolean(),
  free_heap: z.number(),
  min_free_heap: z.number(),
  lighting: z.object({
    hardware_writes: z.number(),
    hardware_errors: z.number(),
    queue_dropped: z.number(),
    max_tick_us: z.number(),
  }),
});

export const I2cScan = z.object({
  busId: z.string(),
  bus: z.number(),
  sda_gpio: z.number(),
  scl_gpio: z.number(),
  devices: z.array(
    z.object({
      address: z.number(),
      address_hex: z.string(),
      known: z.boolean(),
      label: z.string().optional(),
      unexpected: z.boolean(),
    }),
  ),
  missing: z.array(z.string()),
});

/* --------------------------------------------------------------- network */
export const NetworkSettings = z.object({
  hostname: z.string(),
  wifi: z.object({
    ssid: z.string(),
    hasPassword: z.boolean(),
    staticIp: z.object({
      enabled: z.boolean(),
      ip: z.string(),
      netmask: z.string(),
      gateway: z.string(),
      dns: z.string(),
    }),
  }),
  ap: z.object({
    ssid: z.string(),
    policy: z.enum(["sta_fallback_ap", "sta_only", "ap_only", "sta_and_ap"]),
    channel: z.number(),
    security: z.string(),
  }),
  ethernet: z.object({ present: z.boolean(), enabled: z.boolean() }),
  timezone: z.string(),
  ntpServer: z.string(),
});

export const NetworkStatus = z.object({
  wifi: z.object({
    configured: z.boolean(),
    connected: z.boolean(),
    ssid: z.string(),
    ip: z.string(),
    gateway: z.string(),
    netmask: z.string(),
    rssi: z.number(),
    reconnects: z.number(),
  }),
  ap: z.object({ active: z.boolean(), ssid: z.string(), ip: z.string(), clients: z.number() }),
  ethernet: z.object({ present: z.boolean(), link: z.boolean(), ip: z.string() }),
  hostname: z.string(),
  mac: z.string(),
  timeSynced: z.boolean(),
});

export const WifiNetwork = z.object({
  ssid: z.string(),
  rssi: z.number(),
  channel: z.number(),
  secure: z.boolean(),
});

/* -------------------------------------------------------------- websocket */
export const WsEnvelope = z.object({
  version: z.literal(1),
  event: z.string(),
  timestamp: z.number(),
  data: z.unknown(),
});

export const LightStateEvent = z.object({
  channelId: z.string(),
  state: LightState,
  output: LightState,
  available: z.boolean(),
  effect: z.string(),
});

export const InputStateEvent = z.object({
  id: z.string(),
  name: z.string(),
  state: z.boolean(),
});

export const SystemStatusEvent = z.object({
  uptimeSec: z.number(),
  freeHeap: z.number(),
  minFreeHeap: z.number(),
  wsClients: z.number(),
});

/* ------------------------------------------------------------------ types */
export type SessionInfo = z.infer<typeof SessionInfo>;
export type SystemInfo = z.infer<typeof SystemInfo>;
export type Health = z.infer<typeof Health>;
export type LightType = z.infer<typeof LightType>;
export type DriverType = z.infer<typeof DriverType>;
export type LightState = z.infer<typeof LightState>;
export type Capabilities = z.infer<typeof Capabilities>;
export type Light = z.infer<typeof Light>;
export type LightCreate = z.infer<typeof LightCreate>;
export type LightCommand = z.infer<typeof LightCommand>;
export type Accepted = z.infer<typeof Accepted>;
export type Room = z.infer<typeof Room>;
export type RoomForm = z.infer<typeof RoomForm>;
export type Group = z.infer<typeof Group>;
export type AggregationPolicy = z.infer<typeof AggregationPolicy>;
export type SceneLightState = z.infer<typeof SceneLightState>;
export type Scene = z.infer<typeof Scene>;
export type BoardCandidate = z.infer<typeof BoardCandidate>;
export type BoardResource = z.infer<typeof BoardResource>;
export type BoardPeripheral = z.infer<typeof BoardPeripheral>;
export type BoardProfile = z.infer<typeof BoardProfile>;
export type PinInfo = z.infer<typeof PinInfo>;
export type ValidationEntry = z.infer<typeof ValidationEntry>;
export type ValidationReport = z.infer<typeof ValidationReport>;
export type ApplyResult = z.infer<typeof ApplyResult>;
export type HardwareOutput = z.infer<typeof HardwareOutput>;
export type HardwareInput = z.infer<typeof HardwareInput>;
export type PeripheralHealth = z.infer<typeof PeripheralHealth>;
export type HardwareStatus = z.infer<typeof HardwareStatus>;
export type I2cScan = z.infer<typeof I2cScan>;
export type NetworkSettings = z.infer<typeof NetworkSettings>;
export type NetworkStatus = z.infer<typeof NetworkStatus>;
export type WifiNetwork = z.infer<typeof WifiNetwork>;
export type LightStateEvent = z.infer<typeof LightStateEvent>;
export type InputStateEvent = z.infer<typeof InputStateEvent>;
export type SystemStatusEvent = z.infer<typeof SystemStatusEvent>;
