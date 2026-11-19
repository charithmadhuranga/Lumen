// Live WebSocket state store — connection status, event handlers.
// Patches Query cache via queryClient.setQueryData (no second copy of server data).
// DESIGN §8.4.
import { create } from "zustand";
import { subscribeWithSelector } from "zustand/middleware";

import type { ConnectionStatus } from "@/services/ws";

/* ================================================================
   State
   ================================================================ */
interface LiveState {
  // Connection
  status: ConnectionStatus;
  lastError: string | null;

  // Pending commands (for throttling)
  pendingCommands: Map<string, { id: string; timestamp: number }>;

  // Actions
  setStatus: (status: ConnectionStatus) => void;
  setError: (error: string | null) => void;
  addPendingCommand: (id: string) => void;
  removePendingCommand: (id: string) => void;
  clearPendingCommands: () => void;
}

/* ================================================================
   Store
   ================================================================ */
const createLiveState = (set: (partial: Partial<LiveState> | ((state: LiveState) => Partial<LiveState>)) => void): LiveState => ({
  status: "disconnected",
  lastError: null,
  pendingCommands: new Map<string, { id: string; timestamp: number }>(),

  setStatus: (status: ConnectionStatus) => set({ status }),
  setError: (error: string | null) => set({ lastError: error }),

  addPendingCommand: (id: string) =>
    set((state: LiveState) => {
      const next = new Map(state.pendingCommands);
      next.set(id, { id, timestamp: Date.now() });
      return { pendingCommands: next };
    }),

  removePendingCommand: (id: string) =>
    set((state: LiveState) => {
      const next = new Map(state.pendingCommands);
      next.delete(id);
      return { pendingCommands: next };
    }),

  clearPendingCommands: () => set({ pendingCommands: new Map<string, { id: string; timestamp: number }>() }),
});

export const useLiveStore = create<LiveState>()(
  subscribeWithSelector(createLiveState)
);

/* ================================================================
   Selectors
   ================================================================ */
export const useConnectionStatus = () => useLiveStore((s: LiveState) => s.status);
export const useConnectionError = () => useLiveStore((s: LiveState) => s.lastError);
export const usePendingCommands = () => useLiveStore((s: LiveState) => s.pendingCommands);
export const useHasPendingCommands = () => useLiveStore((s: LiveState) => s.pendingCommands.size > 0);

/* ================================================================
   Throttling helpers (DESIGN §8.2: ≤1 command/100ms during drag)
   ================================================================ */
const THROTTLE_MS = 100;

export function useCommandThrottle(commandId: string): {
  canSend: boolean;
  markSent: () => void;
} {
  const pendingCommands = usePendingCommands();
  const addPending = useLiveStore((s: LiveState) => s.addPendingCommand);
  const removePending = useLiveStore((s: LiveState) => s.removePendingCommand);

  const entry = pendingCommands.get(commandId);
  // eslint-disable-next-line react-hooks/purity
  const now = Date.now();
  const canSend = !entry || now - entry.timestamp >= THROTTLE_MS;

  const markSent = () => {
    addPending(commandId);
    // Auto-remove after throttle window
    setTimeout(() => removePending(commandId), THROTTLE_MS);
  };

  return { canSend, markSent };
}