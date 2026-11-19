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