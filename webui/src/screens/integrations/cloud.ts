// Lumen Cloud link of this controller (Lumen Pro; docs/pro/DECISIONS.md D-17).
// The controller never talks to the cloud itself: it is paired with a Lumen
// Gateway on the local network, which represents it.
import { z } from "zod";

import { request } from "@/services/api";

export const CloudStatus = z.object({
  mode: z.enum(["off", "gateway"]),
  state: z.enum(["off", "pairing", "paired", "pair_failed"]),
  lastError: z.string().nullable(),
  deviceId: z.string().optional(),
  gatewayId: z.string().optional(),
  gatewayUrl: z.string().optional(),
  ticketEpoch: z.number().int().optional(),
  pairedAt: z.number().optional(),
  gatewaySeenSecAgo: z.number().nullable(),
  tickets: z.object({ accepted: z.number(), rejected: z.number(), rejectedBy: z.record(z.string(), z.number()) }),
});
export type CloudStatusT = z.infer<typeof CloudStatus>;

// Same rules as the firmware (services/cloud_link.cpp).
export const PairForm = z.object({
  claimToken: z
    .string()
    .trim()
    .regex(/^lct_[A-Za-z0-9_-]{43}$/, "Paste the claim code from the Lumen app (it starts with lct_)"),
  gatewayUrl: z
    .string()
    .trim()
    .regex(
      /^http:\/\/(10\.\d{1,3}|172\.(1[6-9]|2\d|3[01])|192\.168|169\.254)\.\d{1,3}\.\d{1,3}(:\d{1,5})?$/,
      "The gateway's local address, e.g. http://192.168.1.20:8780",
    ),
});
export type PairFormT = z.infer<typeof PairForm>;

export const cloudKey = ["cloud"] as const;

export const cloudApi = {
  get: () => request("GET", "/cloud", CloudStatus),
  pair: (body: PairFormT) => request("POST", "/cloud/pair", CloudStatus, body),
  unpair: () => request("POST", "/cloud/unpair", CloudStatus),
};
