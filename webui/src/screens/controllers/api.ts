// Multi-controller (ESP-NOW) API — loaded with the Devices / Integrations screens.
import { z } from "zod";

import { request } from "@/services/api";
import { LightState, type LightCommand } from "@/schemas";

export const RemoteLight = z.object({ id: z.string(), name: z.string(), type: z.string(), output: LightState });
export const PeerSchema = z.object({
  id: z.string(),
  ip: z.string(),
  version: z.string(),
  online: z.boolean(),
  lastSeenSec: z.number(),
  rssi: z.number(),
  lights: z.array(RemoteLight),
});
export type PeerT = z.infer<typeof PeerSchema>;

export const Controllers = z.object({
  self: z.object({ id: z.string(), enabled: z.boolean(), running: z.boolean(), networkId: z.string(), maxPeers: z.number(), hasKey: z.boolean() }),
  peers: z.array(PeerSchema),
  stats: z.record(z.string(), z.number()),
});
export type ControllersT = z.infer<typeof Controllers>;

export const controllersKey = ["controllers"] as const;
const enc = encodeURIComponent;

export const controllersApi = {
  get: () => request("GET", "/controllers", Controllers),
  command: (peer: string, light: string, command: LightCommand) =>
    request("POST", `/controllers/${enc(peer)}/lights/${enc(light)}/command`, z.object({ sent: z.boolean() }), command),
  networkScene: (scene: string) => request("POST", `/controllers/scenes/${enc(scene)}/activate`, z.object({ local: z.boolean(), broadcast: z.boolean() })),
  update: (body: { enabled?: boolean; networkId?: string; key?: string }) => request("PUT", "/integrations/espnow", Controllers, body),
  key: () => request("GET", "/integrations/espnow/key", z.object({ key: z.string() })),
  regenerateKey: () => request("POST", "/integrations/espnow/key/regenerate", z.object({ key: z.string() })),
};
