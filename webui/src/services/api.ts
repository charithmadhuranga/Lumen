// Single typed fetch client — unwraps {success, data, error} envelope once,
// throws typed ApiError, credentials: "same-origin", CSRF header on mutations.
// SPEC §17.1, DESIGN §8.1.
import { z } from "zod";

/* ================================================================
   Error types
   ================================================================ */
export class ApiError extends Error {
  constructor(
    public readonly code: string,
    public override readonly message: string,
    public readonly status: number,
    public readonly response?: Response
  ) {
    super(`${code}: ${message}`);
    this.name = "ApiError";
  }
}

export class NetworkError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "NetworkError";
  }
}

/* ================================================================
   CSRF token handling
   ================================================================ */
let csrfToken: string | null = null;

export function setCsrfToken(token: string | null) {
  csrfToken = token;
}

function getCsrfToken(): string | null {
  return csrfToken;
}

/* ================================================================
   Fetch wrapper
   ================================================================ */
async function fetchJson<T>(
  url: string,
  options: RequestInit & { schema?: z.ZodType<T> } = {}
): Promise<T> {
  const { schema, headers, ...init } = options;

  const requestHeaders = new Headers(headers);
  requestHeaders.set("Accept", "application/json");
  requestHeaders.set("Content-Type", "application/json");

  // CSRF token for mutating requests
  const method = init.method?.toUpperCase() ?? "GET";
  if (["POST", "PUT", "PATCH", "DELETE"].includes(method)) {
    const token = getCsrfToken();
    if (token) requestHeaders.set("X-CSRF-Token", token);
  }

  let response: Response;
  try {
    response = await fetch(url, {
      ...init,
      headers: requestHeaders,
      credentials: "same-origin",
    });
  } catch (e) {
    throw new NetworkError(`Network error: ${(e as Error).message}`);
  }

  // 401 → clear session, redirect to login
  if (response.status === 401) {
    setCsrfToken(null);
    if (typeof window !== "undefined" && !window.location.pathname.startsWith("/login")) {
      window.location.href = "/login?expired=1";
    }
    throw new ApiError("UNAUTHORIZED", "Session expired", 401, response);
  }

  // Parse JSON envelope
  let json: unknown;
  try {
    json = await response.json();
  } catch {
    throw new ApiError("INVALID_RESPONSE", "Response is not valid JSON", response.status, response);
  }

  // Validate envelope structure
  const envelope = z.object({
    success: z.boolean(),
    data: z.unknown().optional(),
    error: z.object({ code: z.string(), message: z.string() }).optional(),
  }).safeParse(json);

  if (!envelope.success) {
    throw new ApiError("INVALID_RESPONSE", "Response envelope validation failed", response.status, response);
  }

  const { success, data, error } = envelope.data;

  if (!success) {
    throw new ApiError(
      error?.code ?? "UNKNOWN_ERROR",
      error?.message ?? "Unknown error",
      response.status,
      response
    );
  }

  // Validate data against schema if provided
  if (schema && data !== undefined) {
    const parsed = schema.safeParse(data);
    if (!parsed.success) {
      throw new ApiError("INVALID_RESPONSE", `Data validation failed: ${parsed.error.message}`, response.status, response);
    }
    return parsed.data;
  }

  return data as T;
}

/* ================================================================
   Auth API
   ================================================================ */
import { LoginRequest, LoginResponse, PasswordChangeRequest, SessionInfo } from "@/schemas";

export const authApi = {
  login(username: string, password: string) {
    return fetchJson<LoginResponse>("/api/v1/auth/login", {
      method: "POST",
      body: JSON.stringify({ username, password } as LoginRequest),
      schema: LoginResponse,
    });
  },

  logout() {
    return fetchJson<void>("/api/v1/auth/logout", {
      method: "POST",
    });
  },

  session() {
    return fetchJson<SessionInfo>("/api/v1/auth/session", {
      schema: SessionInfo,
    });
  },

  changePassword(currentPassword: string, newPassword: string) {
    return fetchJson<void>("/api/v1/auth/password", {
      method: "POST",
      body: JSON.stringify({ currentPassword, newPassword } as PasswordChangeRequest),
    });
  },
};

/* ================================================================
   System API
   ================================================================ */
import { SystemInfo, HealthResponse } from "@/schemas";

export const systemApi = {
  info() {
    return fetchJson<SystemInfo>("/api/v1/system/info", {
      schema: SystemInfo,
    });
  },

  health() {
    return fetchJson<HealthResponse>("/api/v1/system/health", {
      schema: HealthResponse,
    });
  },
};

/* ================================================================
   Lights API
   ================================================================ */
import { LightChannel, LightCommand, LightCommandResponse } from "@/schemas";

export const lightsApi = {
  list() {
    return fetchJson<LightChannel[]>("/api/v1/lights", {
      schema: z.array(LightChannel),
    });
  },

  get(id: string) {
    return fetchJson<LightChannel>(`/api/v1/lights/${id}`, {
      schema: LightChannel,
    });
  },

  create(channel: Omit<LightChannel, "state" | "requested">) {
    return fetchJson<LightChannel>("/api/v1/lights", {
      method: "POST",
      body: JSON.stringify(channel),
      schema: LightChannel,
    });
  },

  update(id: string, updates: Partial<LightChannel>) {
    return fetchJson<LightChannel>(`/api/v1/lights/${id}`, {
      method: "PUT",
      body: JSON.stringify(updates),
      schema: LightChannel,
    });
  },

  delete(id: string) {
    return fetchJson<void>(`/api/v1/lights/${id}`, {
      method: "DELETE",
    });
  },

  command(id: string, cmd: LightCommand) {
    return fetchJson<LightCommandResponse>(`/api/v1/lights/${id}/command`, {
      method: "POST",
      body: JSON.stringify(cmd),
      schema: LightCommandResponse,
    });
  },
};

/* ================================================================
   Rooms API
   ================================================================ */
import { Room, RoomCreate, RoomUpdate } from "@/schemas";

export const roomsApi = {
  list() {
    return fetchJson<Room[]>("/api/v1/rooms", {
      schema: z.array(Room),
    });
  },

  get(id: string) {
    return fetchJson<Room>(`/api/v1/rooms/${id}`, {
      schema: Room,
    });
  },

  create(room: RoomCreate) {
    return fetchJson<Room>("/api/v1/rooms", {
      method: "POST",
      body: JSON.stringify(room),
      schema: Room,
    });
  },

  update(id: string, updates: RoomUpdate) {
    return fetchJson<Room>(`/api/v1/rooms/${id}`, {
      method: "PUT",
      body: JSON.stringify(updates),
      schema: Room,
    });
  },

  delete(id: string) {
    return fetchJson<void>(`/api/v1/rooms/${id}`, {
      method: "DELETE",
    });
  },

  setPower(id: string, power: boolean, transitionMs?: number) {
    return fetchJson<void>(`/api/v1/rooms/${id}/power`, {
      method: "POST",
      body: JSON.stringify({ power, transitionMs }),
    });
  },

  setBrightness(id: string, brightness: number, transitionMs?: number) {
    return fetchJson<void>(`/api/v1/rooms/${id}/brightness`, {
      method: "POST",
      body: JSON.stringify({ brightness, transitionMs }),
    });
  },
};

/* ================================================================
   Groups API
   ================================================================ */
import { Group, GroupCreate, GroupUpdate } from "@/schemas";

export const groupsApi = {
  list() {
    return fetchJson<Group[]>("/api/v1/groups", {
      schema: z.array(Group),
    });
  },

  get(id: string) {
    return fetchJson<Group>(`/api/v1/groups/${id}`, {
      schema: Group,
    });
  },

  create(group: GroupCreate) {
    return fetchJson<Group>("/api/v1/groups", {
      method: "POST",
      body: JSON.stringify(group),
      schema: Group,
    });
  },

  update(id: string, updates: GroupUpdate) {
    return fetchJson<Group>(`/api/v1/groups/${id}`, {
      method: "PUT",
      body: JSON.stringify(updates),
      schema: Group,
    });
  },

  delete(id: string) {
    return fetchJson<void>(`/api/v1/groups/${id}`, {
      method: "DELETE",
    });
  },

  command(id: string, cmd: LightCommand) {
    return fetchJson<LightCommandResponse>(`/api/v1/groups/${id}/command`, {
      method: "POST",
      body: JSON.stringify(cmd),
      schema: LightCommandResponse,
    });
  },

  setPower(id: string, power: boolean, transitionMs?: number) {
    return fetchJson<void>(`/api/v1/groups/${id}/power`, {
      method: "POST",
      body: JSON.stringify({ power, transitionMs }),
    });
  },

  setBrightness(id: string, brightness: number, transitionMs?: number) {
    return fetchJson<void>(`/api/v1/groups/${id}/brightness`, {
      method: "POST",
      body: JSON.stringify({ brightness, transitionMs }),
    });
  },
};

/* ================================================================
   Scenes API
   ================================================================ */
import { Scene, SceneCreate, SceneActivate } from "@/schemas";

export const scenesApi = {
  list() {
    return fetchJson<Scene[]>("/api/v1/scenes", {
      schema: z.array(Scene),
    });
  },

  get(id: string) {
    return fetchJson<Scene>(`/api/v1/scenes/${id}`, {
      schema: Scene,
    });
  },

  create(scene: SceneCreate) {
    return fetchJson<Scene>("/api/v1/scenes", {
      method: "POST",
      body: JSON.stringify(scene),
      schema: Scene,
    });
  },

  update(id: string, updates: Partial<Scene>) {
    return fetchJson<Scene>(`/api/v1/scenes/${id}`, {
      method: "PUT",
      body: JSON.stringify(updates),
      schema: Scene,
    });
  },

  delete(id: string) {
    return fetchJson<void>(`/api/v1/scenes/${id}`, {
      method: "DELETE",
    });
  },

  activate(id: string, params?: SceneActivate) {
    return fetchJson<void>(`/api/v1/scenes/${id}/activate`, {
      method: "POST",
      body: JSON.stringify(params ?? {}),
    });
  },

  duplicate(id: string) {
    return fetchJson<Scene>(`/api/v1/scenes/${id}/duplicate`, {
      method: "POST",
      schema: Scene,
    });
  },
};