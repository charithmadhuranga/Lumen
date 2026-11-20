// WebSocket client — /api/v1/ws, exponential backoff reconnect,
// Zod-validate each message, patch Query cache via setQueryData.
// DESIGN §8.4, SPEC §17.3.
import { EventEmitter } from "events";
import { z } from "zod";

import type { QueryClient } from "@tanstack/react-query";
import type { LightStateChangedData, WsEventType } from "@/schemas";

/* ================================================================
   Connection states
   ================================================================ */
export type ConnectionStatus = "connecting" | "connected" | "disconnected" | "failed";

/* ================================================================
   Zod schemas for WS events
   ================================================================ */
const LightStateChangedSchema = z.object({
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

const EventSchemas: Record<WsEventType, z.ZodSchema> = {
  light_state_changed: LightStateChangedSchema,
  group_state_changed: z.unknown(),
  scene_activated: z.unknown(),
  system_status: z.unknown(),
  config_changed: z.unknown(),
};

/* ================================================================
   WebSocket Client
   ================================================================ */
interface WsClientOptions {
  url: string;
  onStatusChange?: (status: ConnectionStatus) => void;
  onMessage?: (event: WsEventType, data: unknown) => void;
  queryClient?: QueryClient; // TanStack QueryClient for cache patching
}

export class WsClient extends EventEmitter {
  private ws: WebSocket | null = null;
  private status: ConnectionStatus = "disconnected";
  private reconnectAttempt = 0;
  private reconnectTimeout: ReturnType<typeof setTimeout> | null = null;
  private pingInterval: ReturnType<typeof setTimeout> | null = null;
  private readonly url: string;
  private readonly onStatusChange?: (status: ConnectionStatus) => void;
  private readonly onMessage?: (event: WsEventType, data: unknown) => void;
  private readonly queryClient?: QueryClient;

  // Reconnect config
  private readonly baseDelay = 1000; // 1s
  private readonly maxDelay = 30000; // 30s
  private readonly maxAttempts = Infinity;

  constructor(options: WsClientOptions) {
    super();
    this.url = options.url;
    this.onStatusChange = options.onStatusChange;
    this.onMessage = options.onMessage;
    this.queryClient = options.queryClient;
  }

  connect() {
    if (this.ws?.readyState === WebSocket.OPEN || this.ws?.readyState === WebSocket.CONNECTING) {
      return;
    }

    this.setStatus("connecting");

    try {
      this.ws = new WebSocket(this.url);
    } catch {
      this.setStatus("failed");
      return;
    }

    this.ws.onopen = () => {
      this.reconnectAttempt = 0;
      this.setStatus("connected");
      this.startPing();
    };

    this.ws.onclose = () => {
      this.stopPing();
      if (this.status !== "failed") {
        this.setStatus("disconnected");
        this.scheduleReconnect(true); // Reconnect on close
      }
    };

    this.ws.onerror = () => {
      // Error event doesn't give details; onclose will fire after
    };

    this.ws.onmessage = (event) => this.handleMessage(event.data);
  }

  disconnect() {
    this.stopPing();
    if (this.reconnectTimeout) {
      clearTimeout(this.reconnectTimeout);
      this.reconnectTimeout = null;
    }
    if (this.ws) {
      this.ws.close(1000, "Client disconnect");
      this.ws = null;
    }
    this.setStatus("disconnected");
  }

  private setStatus(status: ConnectionStatus) {
    if (this.status !== status) {
      this.status = status;
      this.onStatusChange?.(status);
      this.emit("status", status);
    }
  }

  private scheduleReconnect(shouldReconnect: boolean) {
    if (!shouldReconnect || this.reconnectAttempt >= this.maxAttempts) {
      this.setStatus("failed");
      return;
    }

    const delay = Math.min(
      this.baseDelay * Math.pow(2, this.reconnectAttempt) + Math.random() * 1000,
      this.maxDelay
    );

    this.reconnectAttempt++;
    this.reconnectTimeout = setTimeout(() => this.connect(), delay);
  }

  private startPing() {
    this.pingInterval = setInterval(() => {
      if (this.ws?.readyState === WebSocket.OPEN) {
        this.ws.send(JSON.stringify({ type: "ping" }));
      }
    }, 30_000);
  }

  private stopPing() {
    if (this.pingInterval) {
      clearInterval(this.pingInterval);
      this.pingInterval = null;
    }
  }

  private handleMessage(rawData: string | ArrayBuffer) {
    const text = typeof rawData === "string" ? rawData : new TextDecoder().decode(rawData);

    let envelope: unknown;
    try {
      envelope = JSON.parse(text);
    } catch {
      console.warn("[WS] Invalid JSON:", text.slice(0, 200));
      return;
    }

    // Validate envelope
    const parsed = z
      .object({
        version: z.number().int().min(1),
        event: z.string(),
        timestamp: z.number().int(),
        data: z.unknown(),
      })
      .safeParse(envelope);

    if (!parsed.success) {
      console.warn("[WS] Envelope validation failed:", parsed.error.message);
      return;
    }

    const { event, data } = parsed.data;

    // Validate event data
    const schema = EventSchemas[event as WsEventType];
    if (schema) {
      const dataParsed = schema.safeParse(data);
      if (!dataParsed.success) {
        console.warn(`[WS] Event ${event} data validation failed:`, dataParsed.error.message);
        return;
      }
    }

    // Patch Query cache if available
    this.patchQueryCache(event, data);

    // Call custom handler
    this.onMessage?.(event as WsEventType, data);

    // Emit for any other listeners
    this.emit(event, data);
    this.emit("message", { event, data });
  }

  private patchQueryCache(event: string, data: unknown) {
    if (!this.queryClient) return;

    switch (event) {
      case "light_state_changed": {
        const d = data as LightStateChangedData;
        this.queryClient.setQueryData(["lights", d.channelId], (old: unknown) => {
          if (!old || typeof old !== "object") return old;
          return {
            ...old,
            state: d.state,
            requested: d.state, // Assume requested ≈ applied for UI
          };
        });
        // Also update in lights list
        this.queryClient.setQueryData(["lights"], (old: unknown) => {
          if (!Array.isArray(old)) return old;
          return old.map((light) =>
            light && typeof light === "object" && light.id === d.channelId
              ? { ...light, state: d.state, requested: d.state }
              : light
          );
        });
        break;
      }
      case "group_state_changed": {
        // Group state patching - similar pattern
        break;
      }
      case "config_changed": {
        // Invalidate relevant queries
        this.queryClient.invalidateQueries({ queryKey: ["lights"] });
        this.queryClient.invalidateQueries({ queryKey: ["rooms"] });
        this.queryClient.invalidateQueries({ queryKey: ["groups"] });
        this.queryClient.invalidateQueries({ queryKey: ["scenes"] });
        break;
      }
    }
  }

  getStatus(): ConnectionStatus {
    return this.status;
  }

  isConnected(): boolean {
    return this.status === "connected";
  }
}

/* ================================================================
   Singleton factory
   ================================================================ */
let wsClientInstance: WsClient | null = null;

export function createWsClient(options: WsClientOptions): WsClient {
  if (wsClientInstance) {
    wsClientInstance.disconnect();
  }
  wsClientInstance = new WsClient(options);
  return wsClientInstance;
}

export function getWsClient(): WsClient | null {
  return wsClientInstance;
}