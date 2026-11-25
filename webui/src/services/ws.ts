// WebSocket client (DESIGN §8.4, SPEC §17.3).
// - single connection to /api/v1/ws, authenticated by the session cookie
// - versioned envelope validated with Zod; unknown events ignored
// - exponential backoff 1 s -> 30 s with jitter; on reconnect the light
//   list is refetched so nothing missed while offline is stale
// - light_state_changed patches the TanStack Query cache directly
//   (setQueryData) and is coalesced to one cache write per animation frame
import type { QueryClient } from "@tanstack/react-query";

import { queryKeys } from "@/services/queryKeys";
import { InputStateEvent, LightStateEvent, SystemStatusEvent, WsEnvelope } from "@/schemas";
import type { Light, LightStateEvent as LightStateEventT } from "@/schemas";
import { useLiveStore } from "@/stores/liveStore";

export class LumenSocket {
  private ws: WebSocket | null = null;
  private attempt = 0;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private ping: ReturnType<typeof setInterval> | null = null;
  private stopped = true;
  private pending = new Map<string, LightStateEventT>();
  private frame = 0;

  constructor(private readonly queryClient: QueryClient) {}

  start(): void {
    if (!this.stopped) return;
    this.stopped = false;
    this.open();
  }

  stop(): void {
    this.stopped = true;
    if (this.timer) clearTimeout(this.timer);
    if (this.ping) clearInterval(this.ping);
    this.timer = null;
    this.ping = null;
    this.ws?.close();
    this.ws = null;
    useLiveStore.getState().setStatus("idle");
  }

  private open(): void {
    const proto = window.location.protocol === "https:" ? "wss" : "ws";
    const store = useLiveStore.getState();
    store.setStatus(this.attempt === 0 ? "connecting" : "reconnecting");
    const ws = new WebSocket(`${proto}://${window.location.host}/api/v1/ws`);
    this.ws = ws;

    ws.onopen = () => {
      const wasReconnect = this.attempt > 0;
      this.attempt = 0;
      useLiveStore.getState().setStatus("connected");
      if (wasReconnect) {
        void this.queryClient.invalidateQueries();
      }
      if (this.ping) clearInterval(this.ping);
      this.ping = setInterval(() => {
        if (ws.readyState === WebSocket.OPEN) ws.send('{"type":"ping"}');
      }, 25000);
    };

    ws.onmessage = (msg) => this.handle(msg.data);

    ws.onclose = () => {
      if (this.ping) clearInterval(this.ping);
      this.ping = null;
      if (this.stopped) return;
      this.attempt += 1;
      useLiveStore.getState().setStatus(this.attempt > 3 ? "offline" : "reconnecting");
      const base = Math.min(30000, 1000 * 2 ** Math.min(this.attempt - 1, 5));
      const delay = base / 2 + Math.random() * (base / 2);
      this.timer = setTimeout(() => this.open(), delay);
    };

    ws.onerror = () => ws.close();
  }

  private handle(raw: unknown): void {
    if (typeof raw !== "string") return;
    let json: unknown;
    try {
      json = JSON.parse(raw);
    } catch {
      return;
    }
    const env = WsEnvelope.safeParse(json);
    if (!env.success) return;
    const store = useLiveStore.getState();
    store.touch();
    const { event, data } = env.data;
    switch (event) {
      case "light_state_changed": {
        const parsed = LightStateEvent.safeParse(data);
        if (parsed.success) this.queueLight(parsed.data);
        break;
      }
      case "input_state_changed": {
        const parsed = InputStateEvent.safeParse(data);
        if (parsed.success) store.setInput(parsed.data.id, parsed.data.state);
        break;
      }
      case "system_status": {
        const parsed = SystemStatusEvent.safeParse(data);
        if (parsed.success) store.setSystem(parsed.data);
        break;
      }
      case "config_changed":
        void this.queryClient.invalidateQueries({ queryKey: queryKeys.lights() });
        void this.queryClient.invalidateQueries({ queryKey: queryKeys.rooms() });
        break;
      case "scene_activated":
      case "group_state_changed":
        void this.queryClient.invalidateQueries({ queryKey: queryKeys.rooms() });
        void this.queryClient.invalidateQueries({ queryKey: queryKeys.groups() });
        break;
      case "hardware_status_changed":
        void this.queryClient.invalidateQueries({ queryKey: ["hardware"] });
        void this.queryClient.invalidateQueries({ queryKey: ["boards"] });
        break;
      case "network_status":
        void this.queryClient.invalidateQueries({ queryKey: queryKeys.networkStatus() });
        break;
      default:
        break; // unknown events are ignored (forward compatible)
    }
  }

  private queueLight(e: LightStateEventT): void {
    this.pending.set(e.channelId, e);
    if (this.frame) return;
    this.frame = requestAnimationFrame(() => {
      this.frame = 0;
      const updates = this.pending;
      this.pending = new Map();
      this.queryClient.setQueryData<Light[]>(queryKeys.lights(), (old) =>
        old?.map((l) => {
          const u = updates.get(l.id);
          return u ? { ...l, state: u.state, output: u.output, available: u.available, effect: u.effect || undefined } : l;
        }),
      );
      // Room aggregates derive from light state: refresh lazily.
      void this.queryClient.invalidateQueries({ queryKey: queryKeys.rooms(), refetchType: "none" });
    });
  }
}
