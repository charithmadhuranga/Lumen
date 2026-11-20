// Zod schemas mirroring firmware validators — single source of truth for API types.
// SPEC §17, DESIGN §8.1. All responses parsed with these; z.infer used everywhere.
import { z } from "zod";

/* ================================================================
   Common / Envelope
   ================================================================ */
export const ApiEnvelope = <T extends z.ZodTypeAny>(schema: T) =>
  z.object({
    success: z.boolean(),
    data: schema.optional(),
    error: z
      .object({
        code: z.string(),
        message: z.string(),
      })
      .optional(),
  });

export type ApiEnvelope<T> = {
  success: boolean;
  data?: T;
  error?: { code: string; message: string };
};

/* ================================================================
   Auth
   ================================================================ */
export const LoginRequest = z.object({
  username: z.string().min(1),
  password: z.string().min(1),
});

export const LoginResponse = z.object({
  token: z.string(),
  username: z.string(),
  forcePasswordChange: z.boolean(),
});

export const PasswordChangeRequest = z.object({
  currentPassword: z.string().min(1),
  newPassword: z.string().min(10),
});

export const SessionInfo = z.object({
  token: z.string(),
  username: z.string(),
  forcePasswordChange: z.boolean(),
});

/* ================================================================
   System
   ================================================================ */
export const SystemInfo = z.object({
  firmwareVersion: z.string(),
  idfVersion: z.string(),
  chipModel: z.string(),
  chipRevision: z.number(),
  cores: z.number(),
  cpuFreqMHz: z.number(),
  flashSize: z.number(),
  freeHeap: z.number(),
  uptimeSec: z.number(),
});

export const HealthResponse = z.object({
  ok: z.boolean(),
  services: z.record(z.string(), z.boolean()),
});

/* ================================================================
   Lights
   ================================================================ */
export const LightType = z.enum([
  "switch",
  "dimmer",
  "rgb",
  "rgbw",
  "cct",
  "addressable_rgb",
  "addressable_rgbw",
]);

export const LightChannel = z.object({
  id: z.string(),
  name: z.string(),
  type: LightType,
  // Current state (actual hardware)
  state: z.object({
    power: z.boolean(),
    brightness: z.number().int().min(0).max(100),
    red: z.number().int().min(0).max(255),
    green: z.number().int().min(0).max(255),
    blue: z.number().int().min(0).max(255),
    white: z.number().int().min(0).max(255),
    colorTemperature: z.number().int().min(1500).max(6500),
  }),
  // Requested state (from last command)
  requested: z.object({
    power: z.boolean(),
    brightness: z.number().int().min(0).max(100),
    red: z.number().int().min(0).max(255),
    green: z.number().int().min(0).max(255),
    blue: z.number().int().min(0).max(255),
    white: z.number().int().min(0).max(255),
    colorTemperature: z.number().int().min(1500).max(6500),
  }).optional(),
  // Calibration
  calibration: z.object({
    brightnessCurve: z.array(z.number()).optional(),
    colorCurve: z.array(z.number()).optional(),
    minBrightness: z.number().optional(),
    maxBrightness: z.number().optional(),
  }).optional(),
});

export const LightCommand = z.object({
  power: z.boolean().optional(),
  brightness: z.number().int().min(0).max(100).optional(),
  red: z.number().int().min(0).max(255).optional(),
  green: z.number().int().min(0).max(255).optional(),
  blue: z.number().int().min(0).max(255).optional(),
  white: z.number().int().min(0).max(255).optional(),
  colorTemperature: z.number().int().min(1500).max(6500).optional(),
  transitionMs: z.number().int().min(0).optional(),
  effect: z.string().optional(),
  effectParams: z.record(z.string(), z.number()).optional(),
  origin: z.string().optional(),
  commandId: z.string().optional(),
});

export const LightCommandResponse = z.object({
  accepted: z.boolean(),
  commandId: z.string().optional(),
});

/* ================================================================
   Rooms
   ================================================================ */
export const Room = z.object({
  id: z.string(),
  name: z.string(),
  floor: z.string().optional(),
  icon: z.string().optional(),
  floorplanId: z.string().optional(),
  channelIds: z.array(z.string()),
  groupIds: z.array(z.string()),
});

export const RoomCreate = z.object({
  id: z.string(),
  name: z.string(),
  floor: z.string().optional(),
  icon: z.string().optional(),
  floorplanId: z.string().optional(),
  channelIds: z.array(z.string()).optional(),
  groupIds: z.array(z.string()).optional(),
});

export const RoomUpdate = z.object({
  name: z.string().optional(),
  floor: z.string().optional(),
  icon: z.string().optional(),
  floorplanId: z.string().optional(),
});

/* ================================================================
   Groups
   ================================================================ */
export const AggregationPolicy = z.enum(["average", "max", "min"]);

export const Group = z.object({
  id: z.string(),
  name: z.string(),
  roomId: z.string().optional(),
  channelIds: z.array(z.string()),
  policy: AggregationPolicy,
});

export const GroupCreate = z.object({
  id: z.string(),
  name: z.string(),
  roomId: z.string().optional(),
  channelIds: z.array(z.string()).optional(),
  policy: AggregationPolicy.optional(),
});

export const GroupUpdate = z.object({
  name: z.string().optional(),
  roomId: z.string().optional(),
  policy: AggregationPolicy.optional(),
});

/* ================================================================
   Scenes
   ================================================================ */
export const SceneLightState = z.object({
  lightId: z.string(),
  power: z.boolean(),
  brightness: z.number().int().min(0).max(100),
  red: z.number().int().min(0).max(255),
  green: z.number().int().min(0).max(255),
  blue: z.number().int().min(0).max(255),
  white: z.number().int().min(0).max(255),
  colorTemperature: z.number().int().min(1500).max(6500),
});

export const Scene = z.object({
  id: z.string(),
  name: z.string(),
  transitionMs: z.number().int().min(0),
  states: z.array(SceneLightState),
});

export const SceneCreate = z.object({
  id: z.string(),
  name: z.string(),
  transitionMs: z.number().int().min(0).optional(),
  states: z.array(SceneLightState),
});

export const SceneActivate = z.object({
  transitionMs: z.number().int().min(0).optional(),
  origin: z.string().optional(),
  commandId: z.string().optional(),
});

/* ================================================================
   Events (WebSocket)
   ================================================================ */
export const WsEventType = z.enum([
  "light_state_changed",
  "group_state_changed",
  "scene_activated",
  "system_status",
  "config_changed",
]);

export const WsEnvelope = z.object({
  version: z.number().int().min(1),
  event: WsEventType,
  timestamp: z.number().int(),
  data: z.unknown(),
});

export const LightStateChangedData = z.object({
  channelId: z.string(),
  state: z.object({
    power: z.boolean(),
    brightness: z.number().int().min(0).max(100),
    red: z.number().int().min(0).max(255),
    green: z.number().int().min(0).max(255),
    blue: z.number().int().min(0).max(255),
    white: z.number().int().min(0).max(255),
    colorTemperature: z.number().int().min(1500).max(6500),
  }),
});

/* ================================================================
   Hardware / Board profiles (Phase 7 — mirrors mockup-ui §7.8)
   ================================================================ */
export const BoardResource = z.object({
  id: z.string(),
  type: z.enum(["gpio", "pwm", "i2c", "uart", "rmt", "ethernet"]),
  gpio: z.number().int().min(-1),
  direction: z.enum(["input", "output", "bidirectional"]),
  inverted: z.boolean().optional(),
  pull: z.enum(["none", "up", "down"]).optional(),
  safe_state: z.boolean().optional(),
});

export const BoardPeripheral = z.object({
  id: z.string(),
  type: z.enum(["pca9685", "pcf8574", "relay", "ethernet", "rs485", "addressable_led"]),
  bus: z.string().optional(),
  address: z.number().int().min(-1).optional(),
  channels: z.number().int().min(0).optional(),
  frequency: z.number().int().min(0).optional(),
  unverified: z.boolean().optional(),
  address_verification_required: z.boolean().optional(),
  unverified_note: z.string().optional(),
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
});

// A single GPIO pin as rendered in the Pin Mapping grid (mockup: gpioPins).
export const BoardPin = z.object({
  gpio: z.number().int(),
  role: z.string(),
  dir: z.enum(["input", "output", "bidirectional", "input-only", "free"]),
  strapping: z.boolean(),
  conflict: z.boolean(),
  input_only: z.boolean().optional(),
  reason: z.string().optional(),
});

// A candidate in the Board tab list (mockup: boardProfiles).
export const BoardCandidate = z.object({
  id: z.string(),
  name: z.string(),
  mcu: z.string(),
  builtIn: z.boolean(),
  desc: z.string().optional(),
  active: z.boolean().optional(),
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
  has_errors: z.boolean().optional(),
  error_count: z.number().int().optional(),
  warning_count: z.number().int().optional(),
});

export const HardwareOutput = z.object({
  id: z.string(),
  name: z.string(),
  driver: z.string(),
  type: z.string(),
  state: z.boolean(),
  value: z.number().optional(),
});

export const HardwareInput = z.object({
  id: z.string(),
  name: z.string(),
  source: z.string(),
  state: z.boolean(),
  last_event: z.string().optional(),
});

export const I2cDevice = z.object({
  address: z.number().int(),
  address_hex: z.string(),
  known: z.boolean(),
  label: z.string().optional(),
  unexpected: z.boolean().optional(),
});

export const I2cScanResult = z.object({
  bus: z.number().int().optional(),
  sda_gpio: z.number().int().optional(),
  scl_gpio: z.number().int().optional(),
  devices: z.array(I2cDevice),
});

export const HardwareStatus = z.object({
  active_board: z.string().optional(),
  active_board_name: z.string().optional(),
  mcu: z.string().optional(),
  outputs: z.array(HardwareOutput).optional(),
  inputs: z.array(HardwareInput).optional(),
  pca9685_present: z.boolean().optional(),
  pcf8574_health: z.record(z.string(), z.number()).optional(),
  ethernet_link: z.boolean().optional(),
  free_heap: z.number().int().optional(),
  min_free_heap: z.number().int().optional(),
});

export const ApplyBoardResult = z.object({
  applied: z.boolean(),
  rolled_back: z.boolean(),
  active_board: z.string().optional(),
  message: z.string().optional(),
  report: ValidationReport.optional(),
});

/* ================================================================
   Type exports (z.infer)
   ================================================================ */
export type LoginRequest = z.infer<typeof LoginRequest>;
export type LoginResponse = z.infer<typeof LoginResponse>;
export type PasswordChangeRequest = z.infer<typeof PasswordChangeRequest>;
export type SessionInfo = z.infer<typeof SessionInfo>;
export type SystemInfo = z.infer<typeof SystemInfo>;
export type HealthResponse = z.infer<typeof HealthResponse>;
export type LightChannel = z.infer<typeof LightChannel>;
export type LightCommand = z.infer<typeof LightCommand>;
export type LightCommandResponse = z.infer<typeof LightCommandResponse>;
export type Room = z.infer<typeof Room>;
export type RoomCreate = z.infer<typeof RoomCreate>;
export type RoomUpdate = z.infer<typeof RoomUpdate>;
export type Group = z.infer<typeof Group>;
export type GroupCreate = z.infer<typeof GroupCreate>;
export type GroupUpdate = z.infer<typeof GroupUpdate>;
export type AggregationPolicy = z.infer<typeof AggregationPolicy>;
export type Scene = z.infer<typeof Scene>;
export type SceneCreate = z.infer<typeof SceneCreate>;
export type SceneActivate = z.infer<typeof SceneActivate>;
export type SceneLightState = z.infer<typeof SceneLightState>;
export type WsEventType = z.infer<typeof WsEventType>;
export type WsEnvelope = z.infer<typeof WsEnvelope>;
export type LightStateChangedData = z.infer<typeof LightStateChangedData>;
export type BoardResource = z.infer<typeof BoardResource>;
export type BoardPeripheral = z.infer<typeof BoardPeripheral>;
export type BoardProfile = z.infer<typeof BoardProfile>;
export type BoardPin = z.infer<typeof BoardPin>;
export type BoardCandidate = z.infer<typeof BoardCandidate>;
export type ValidationEntry = z.infer<typeof ValidationEntry>;
export type ValidationReport = z.infer<typeof ValidationReport>;
export type HardwareOutput = z.infer<typeof HardwareOutput>;
export type HardwareInput = z.infer<typeof HardwareInput>;
export type I2cDevice = z.infer<typeof I2cDevice>;
export type I2cScanResult = z.infer<typeof I2cScanResult>;
export type HardwareStatus = z.infer<typeof HardwareStatus>;
export type ApplyBoardResult = z.infer<typeof ApplyBoardResult>;