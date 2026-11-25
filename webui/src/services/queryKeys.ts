// Query keys — single source of truth (DESIGN §8.3).
export const queryKeys = {
  session: () => ["auth", "session"] as const,
  systemInfo: () => ["system", "info"] as const,
  diagnostics: () => ["system", "diagnostics"] as const,
  lights: () => ["lights"] as const,
  effects: () => ["lights", "effects"] as const,
  rooms: () => ["rooms"] as const,
  groups: () => ["groups"] as const,
  scenes: () => ["scenes"] as const,
  boards: () => ["boards"] as const,
  board: (id: string) => ["boards", id] as const,
  boardPins: (id: string) => ["boards", id, "pins"] as const,
  activeBoard: () => ["boards", "active"] as const,
  hardwareStatus: () => ["hardware", "status"] as const,
  hardwareOutputs: () => ["hardware", "outputs"] as const,
  network: () => ["network"] as const,
  networkStatus: () => ["network", "status"] as const,
} as const;
