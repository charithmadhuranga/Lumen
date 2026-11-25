// Single typed API client (DESIGN §8.1, SPEC §17.1).
// - unwraps {success, data, error} once and throws a typed ApiError
// - same-origin cookies; X-CSRF-Token on every mutating request
// - 401 -> session cleared + redirect to /login
// - every response parsed with its Zod schema
import { z } from "zod";

import * as S from "@/schemas";

export class ApiError extends Error {
  constructor(
    public readonly code: string,
    message: string,
    public readonly status: number,
    public readonly data?: unknown,
    public readonly retryAfter?: number,
  ) {
    super(message);
    this.name = "ApiError";
  }
}

let csrfToken: string | null = null;
let onUnauthorized: (() => void) | null = null;

export function setCsrfToken(token: string | null): void {
  csrfToken = token;
}

/** Called when any request returns 401 (session expired / logged out). */
export function setUnauthorizedHandler(handler: (() => void) | null): void {
  onUnauthorized = handler;
}

const Envelope = z.object({
  success: z.boolean(),
  data: z.unknown().optional(),
  error: z.object({ code: z.string(), message: z.string() }).nullable().optional(),
});

type Method = "GET" | "POST" | "PUT" | "DELETE";

export async function request<T>(
  method: Method,
  path: string,
  schema: z.ZodType<T>,
  body?: unknown,
  options: { raw?: BodyInit; contentType?: string; timeoutMs?: number } = {},
): Promise<T> {
  const headers: Record<string, string> = { Accept: "application/json" };
  if (method !== "GET") {
    if (csrfToken) headers["X-CSRF-Token"] = csrfToken;
    headers["Content-Type"] = options.contentType ?? "application/json";
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), options.timeoutMs ?? 15000);
  let response: Response;
  try {
    response = await fetch(`/api/v1${path}`, {
      method,
      headers,
      credentials: "same-origin",
      body: options.raw ?? (body === undefined ? (method === "GET" ? undefined : "{}") : JSON.stringify(body)),
      signal: controller.signal,
    });
  } catch (e) {
    const aborted = e instanceof DOMException && e.name === "AbortError";
    throw new ApiError(
      aborted ? "TIMEOUT" : "NETWORK_ERROR",
      aborted ? "The controller did not respond in time" : "Cannot reach the controller",
      0,
    );
  } finally {
    clearTimeout(timer);
  }

  let json: unknown;
  try {
    json = await response.json();
  } catch {
    throw new ApiError("INVALID_RESPONSE", `Unexpected response (HTTP ${response.status})`, response.status);
  }
  const env = Envelope.safeParse(json);
  if (!env.success) {
    throw new ApiError("INVALID_RESPONSE", "Malformed response envelope", response.status);
  }
  if (!env.data.success) {
    const err = env.data.error ?? { code: "UNKNOWN_ERROR", message: "Request failed" };
    if (response.status === 401 && path !== "/auth/login" && path !== "/auth/session") {
      setCsrfToken(null);
      onUnauthorized?.();
    }
    const retryAfter = Number(response.headers.get("Retry-After") ?? "") || undefined;
    throw new ApiError(err.code, err.message, response.status, env.data.data, retryAfter);
  }
  const parsed = schema.safeParse(env.data.data ?? null);
  if (!parsed.success) {
    console.error(`[api] ${method} ${path}: response failed validation`, parsed.error.issues);
    throw new ApiError("INVALID_RESPONSE", "The controller sent data this UI does not understand", response.status);
  }
  return parsed.data;
}

const Null = z.unknown();
const IdOnly = z.object({ id: z.string() });

export const api = {
  /* auth */
  login: (username: string, password: string) =>
    request("POST", "/auth/login", S.SessionInfo, { username, password }),
  logout: () => request("POST", "/auth/logout", Null),
  session: () => request("GET", "/auth/session", S.SessionInfo),
  changePassword: (currentPassword: string, newPassword: string) =>
    request("PUT", "/auth/password", S.SessionInfo.nullable(), { currentPassword, newPassword }, { timeoutMs: 30000 }),

  /* system */
  systemInfo: () => request("GET", "/system/info", S.SystemInfo),
  health: () => request("GET", "/system/health", S.Health),
  diagnostics: () => request("GET", "/system/diagnostics", z.record(z.string(), z.unknown())),
  restart: () => request("POST", "/system/restart", z.object({ restarting: z.boolean() })),
  factoryReset: () => request("POST", "/system/factory-reset", z.object({ resetting: z.boolean() }), { confirm: "RESET" }),

  /* lights */
  lights: () => request("GET", "/lights", z.array(S.Light)),
  light: (id: string) => request("GET", `/lights/${encodeURIComponent(id)}`, S.Light),
  createLight: (light: S.LightCreate) => request("POST", "/lights", S.Light, light),
  updateLight: (id: string, patch: Partial<S.LightCreate>) =>
    request("PUT", `/lights/${encodeURIComponent(id)}`, S.Light, patch),
  deleteLight: (id: string) => request("DELETE", `/lights/${encodeURIComponent(id)}`, IdOnly),
  commandLight: (id: string, cmd: S.LightCommand) =>
    request("POST", `/lights/${encodeURIComponent(id)}/command`, S.Accepted, cmd),
  effects: () => request("GET", "/lights/effects", z.array(z.string())),

  /* rooms */
  rooms: () => request("GET", "/rooms", z.array(S.Room)),
  createRoom: (room: S.RoomForm) => request("POST", "/rooms", S.Room, room),
  updateRoom: (id: string, room: Partial<S.RoomForm>) =>
    request("PUT", `/rooms/${encodeURIComponent(id)}`, S.Room, room),
  deleteRoom: (id: string) => request("DELETE", `/rooms/${encodeURIComponent(id)}`, IdOnly),
  roomPower: (id: string, power: boolean, transitionMs?: number) =>
    request("POST", `/rooms/${encodeURIComponent(id)}/power`, S.Accepted, { power, transitionMs }),
  roomBrightness: (id: string, brightness: number, transitionMs?: number) =>
    request("POST", `/rooms/${encodeURIComponent(id)}/brightness`, S.Accepted, { brightness, transitionMs }),

  /* groups */
  groups: () => request("GET", "/groups", z.array(S.Group)),
  createGroup: (g: { name: string; roomId?: string; channelIds: string[]; policy: S.AggregationPolicy }) =>
    request("POST", "/groups", S.Group, g),
  updateGroup: (id: string, g: Partial<{ name: string; roomId: string; channelIds: string[]; policy: S.AggregationPolicy }>) =>
    request("PUT", `/groups/${encodeURIComponent(id)}`, S.Group, g),
  deleteGroup: (id: string) => request("DELETE", `/groups/${encodeURIComponent(id)}`, IdOnly),
  commandGroup: (id: string, cmd: S.LightCommand) =>
    request("POST", `/groups/${encodeURIComponent(id)}/command`, S.Accepted, cmd),

  /* scenes */
  scenes: () => request("GET", "/scenes", z.array(S.Scene)),
  createScene: (s: { name: string; transitionMs: number; states: S.SceneLightState[] }) =>
    request("POST", "/scenes", S.Scene, s),
  captureScene: (name: string, lightIds?: string[], transitionMs = 500) =>
    request("POST", "/scenes/capture", S.Scene, { name, lightIds, transitionMs }),
  updateScene: (id: string, s: Partial<{ name: string; transitionMs: number; states: S.SceneLightState[] }>) =>
    request("PUT", `/scenes/${encodeURIComponent(id)}`, S.Scene, s),
  deleteScene: (id: string) => request("DELETE", `/scenes/${encodeURIComponent(id)}`, IdOnly),
  activateScene: (id: string, transitionMs?: number) =>
    request("POST", `/scenes/${encodeURIComponent(id)}/activate`, S.Accepted, transitionMs ? { transitionMs } : {}),
  duplicateScene: (id: string) => request("POST", `/scenes/${encodeURIComponent(id)}/duplicate`, S.Scene),

  /* boards / hardware */
  boards: () => request("GET", "/boards", z.array(S.BoardCandidate)),
  board: (id: string) => request("GET", `/boards/${encodeURIComponent(id)}`, S.BoardProfile),
  activeBoard: () => request("GET", "/boards/active", S.BoardProfile),
  boardPins: (id: string) => request("GET", `/boards/${encodeURIComponent(id)}/pins`, z.array(S.PinInfo)),
  validateBoard: (profile: S.BoardProfile) => request("POST", "/boards/validate", S.ValidationReport, profile),
  createBoard: (profile: S.BoardProfile) => request("POST", "/boards", S.BoardProfile, profile),
  updateBoard: (id: string, profile: S.BoardProfile) =>
    request("PUT", `/boards/${encodeURIComponent(id)}`, S.BoardProfile, profile),
  deleteBoard: (id: string) => request("DELETE", `/boards/${encodeURIComponent(id)}`, IdOnly),
  duplicateBoard: (id: string, newId?: string, name?: string) =>
    request("POST", `/boards/${encodeURIComponent(id)}/duplicate`, S.BoardProfile, { id: newId, name }),
  applyBoard: (id: string, force = false) =>
    request("POST", `/boards/${encodeURIComponent(id)}/apply`, S.ApplyResult, { force }, { timeoutMs: 30000 }),
  hardwareStatus: () => request("GET", "/hardware/status", S.HardwareStatus),
  hardwareInputs: () => request("GET", "/hardware/inputs", z.array(S.HardwareInput)),
  hardwareOutputs: () => request("GET", "/hardware/outputs", z.array(S.HardwareOutput)),
  testOutput: (id: string, level: number) =>
    request("POST", `/hardware/outputs/${encodeURIComponent(id)}/test`, z.object({ id: z.string(), level: z.number() }), { level }),
  i2cScan: (bus?: string) =>
    request("GET", `/hardware/i2c/scan${bus ? `?bus=${encodeURIComponent(bus)}` : ""}`, S.I2cScan, undefined, { timeoutMs: 20000 }),

  /* network */
  network: () => request("GET", "/network", S.NetworkSettings),
  networkStatus: () => request("GET", "/network/status", S.NetworkStatus),
  wifiScan: () => request("GET", "/network/scan", z.array(S.WifiNetwork), undefined, { timeoutMs: 20000 }),
  apPassword: () => request("GET", "/network/ap-password", z.object({ password: z.string() })),
  updateWifi: (w: { ssid: string; password?: string; staticIp?: S.NetworkSettings["wifi"]["staticIp"] }) =>
    request("PUT", "/network/wifi", z.object({ applying: z.boolean() }), w),
  updateAp: (a: { ssid?: string; password?: string; policy: S.NetworkSettings["ap"]["policy"]; channel?: number }) =>
    request("PUT", "/network/ap", z.object({ applying: z.boolean() }), a),
  updateEthernet: (enabled: boolean) =>
    request("PUT", "/network/ethernet", z.object({ enabled: z.boolean(), restartRequired: z.boolean() }), { enabled }),
  updateNetworkSystem: (s: { hostname?: string; timezone?: string; ntpServer?: string }) =>
    request("PUT", "/network/system", S.NetworkSettings, s),
};

export type Api = typeof api;
