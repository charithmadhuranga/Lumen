// Floor-plan API (loaded with the floor-plan screens only).
import { z } from "zod";

import { request, upload } from "@/services/api";

export const ViewBoxSchema = z.tuple([z.number(), z.number(), z.number().positive(), z.number().positive()]);

export const LightPin = z.object({
  lightId: z.string(),
  x: z.number(),
  y: z.number(),
  radius: z.number().positive(),
  color: z.string().regex(/^#[0-9a-fA-F]{6}$/).optional(),
  icon: z.string().optional(),
});
export type LightPinT = z.infer<typeof LightPin>;

export const RoomRegion = z.object({ roomId: z.string(), points: z.array(z.tuple([z.number(), z.number()])).min(3) });
export type RoomRegionT = z.infer<typeof RoomRegion>;

export const FloorPlan = z.object({
  schema_version: z.number(),
  id: z.string(),
  name: z.string(),
  viewBox: ViewBoxSchema,
  hasSvg: z.boolean(),
  lights: z.array(LightPin),
  rooms: z.array(RoomRegion),
});
export type FloorPlanT = z.infer<typeof FloorPlan>;

export const FloorPlanSummary = z.object({
  id: z.string(),
  name: z.string(),
  viewBox: ViewBoxSchema,
  hasSvg: z.boolean(),
  lights: z.number(),
  rooms: z.number(),
});

export const floorplanKeys = {
  list: ["floorplans"] as const,
  plan: (id: string) => ["floorplans", id] as const,
  svg: (id: string) => ["floorplans", id, "svg"] as const,
};

const enc = encodeURIComponent;

export const floorplanApi = {
  list: () => request("GET", "/floorplans", z.array(FloorPlanSummary)),
  get: (id: string) => request("GET", `/floorplans/${enc(id)}`, FloorPlan),
  create: (name: string) => request("POST", "/floorplans", FloorPlan, { name }),
  rename: (id: string, name: string) => request("PUT", `/floorplans/${enc(id)}`, FloorPlan, { name }),
  remove: (id: string) => request("DELETE", `/floorplans/${enc(id)}`, z.object({ id: z.string() })),
  saveLayout: (id: string, layout: { lights: LightPinT[]; rooms: RoomRegionT[] }) =>
    request("PUT", `/floorplans/${enc(id)}/layout`, FloorPlan, layout),
  uploadSvg: (id: string, sanitizedSvg: string, onProgress?: (f: number) => void) =>
    upload(`/floorplans/${enc(id)}/upload`, new Blob([sanitizedSvg], { type: "image/svg+xml" }), FloorPlan.extend({ elements: z.number(), layoutReset: z.boolean() }), onProgress),
  /** Raw stored SVG text (sanitised again before it is rendered). */
  svg: async (id: string): Promise<string> => {
    const r = await fetch(`/api/v1/floorplans/${enc(id)}/svg`, { credentials: "same-origin" });
    if (!r.ok) throw new Error(r.status === 404 ? "No drawing uploaded" : `Could not load the drawing (HTTP ${r.status})`);
    return r.text();
  },
};
