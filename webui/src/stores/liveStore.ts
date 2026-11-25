// Live state owned by the WebSocket (DESIGN §2.1): connection status,
// input states and latest system telemetry. REST data lives in TanStack
// Query; this store never duplicates it.
import { create } from "zustand";

import type { SystemStatusEvent } from "@/schemas";

export type ConnectionStatus = "idle" | "connecting" | "connected" | "reconnecting" | "offline";

interface LiveState {
  status: ConnectionStatus;
  lastMessageAt: number;
  inputs: Record<string, boolean>;
  system: SystemStatusEvent | null;
  setStatus: (s: ConnectionStatus) => void;
  setInput: (id: string, state: boolean) => void;
  setInputs: (inputs: Record<string, boolean>) => void;
  setSystem: (s: SystemStatusEvent) => void;
  touch: () => void;
}

export const useLiveStore = create<LiveState>()((set) => ({
  status: "idle",
  lastMessageAt: 0,
  inputs: {},
  system: null,
  setStatus: (status) => set({ status }),
  setInput: (id, state) => set((s) => ({ inputs: { ...s.inputs, [id]: state } })),
  setInputs: (inputs) => set({ inputs }),
  setSystem: (system) => set({ system }),
  touch: () => set({ lastMessageAt: Date.now() }),
}));

export const useConnectionStatus = () => useLiveStore((s) => s.status);
