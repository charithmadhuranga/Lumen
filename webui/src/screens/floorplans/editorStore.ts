// Floor-plan editor state (Zustand owns UI/editor state, AGENTS.md §9):
// the draft layout, selection and a bounded undo/redo history. Saved data
// stays in TanStack Query; this is only the unsaved draft.
import { create } from "zustand";

import type { LightPinT, RoomRegionT } from "./api";

export interface Draft {
  lights: LightPinT[];
  rooms: RoomRegionT[];
}

const HISTORY = 50;

interface EditorState {
  planId: string | null;
  draft: Draft;
  saved: Draft;
  past: Draft[];
  future: Draft[];
  selected: string[]; // light ids or "room:<id>"
  load: (planId: string, d: Draft) => void;
  /** Apply a change; `coalesce` merges it into the previous step (drags). */
  change: (fn: (d: Draft) => Draft, coalesce?: boolean) => void;
  undo: () => void;
  redo: () => void;
  select: (ids: string[]) => void;
  markSaved: (d: Draft) => void;
}

const clone = (d: Draft): Draft => ({ lights: d.lights.map((l) => ({ ...l })), rooms: d.rooms.map((r) => ({ ...r, points: r.points.map((p) => [p[0], p[1]] as [number, number]) })) });

export const useEditorStore = create<EditorState>()((set) => ({
  planId: null,
  draft: { lights: [], rooms: [] },
  saved: { lights: [], rooms: [] },
  past: [],
  future: [],
  selected: [],
  load: (planId, d) => set({ planId, draft: clone(d), saved: clone(d), past: [], future: [], selected: [] }),
  change: (fn, coalesce = false) =>
    set((s) => {
      const next = fn(clone(s.draft));
      const past = coalesce && s.past.length > 0 ? s.past : [...s.past, s.draft].slice(-HISTORY);
      return { draft: next, past, future: [] };
    }),
  undo: () =>
    set((s) => {
      if (s.past.length === 0) return s;
      const prev = s.past[s.past.length - 1];
      return { draft: prev, past: s.past.slice(0, -1), future: [s.draft, ...s.future].slice(0, HISTORY) };
    }),
  redo: () =>
    set((s) => {
      if (s.future.length === 0) return s;
      const [next, ...rest] = s.future;
      return { draft: next, past: [...s.past, s.draft].slice(-HISTORY), future: rest };
    }),
  select: (selected) => set({ selected }),
  markSaved: (d) => set({ saved: clone(d), draft: clone(d) }),
}));

export function isDirty(s: Pick<EditorState, "draft" | "saved">) {
  return JSON.stringify(s.draft) !== JSON.stringify(s.saved);
}

/** Align selected light pins (DESIGN §9.4). */
export function align(d: Draft, ids: string[], how: "left" | "centerX" | "right" | "top" | "centerY" | "bottom"): Draft {
  const pins = d.lights.filter((l) => ids.includes(l.lightId));
  if (pins.length < 2) return d;
  const xs = pins.map((p) => p.x);
  const ys = pins.map((p) => p.y);
  const target =
    how === "left" ? Math.min(...xs) : how === "right" ? Math.max(...xs) : how === "centerX" ? (Math.min(...xs) + Math.max(...xs)) / 2
    : how === "top" ? Math.min(...ys) : how === "bottom" ? Math.max(...ys) : (Math.min(...ys) + Math.max(...ys)) / 2;
  const horizontal = how === "left" || how === "right" || how === "centerX";
  return { ...d, lights: d.lights.map((l) => (ids.includes(l.lightId) ? { ...l, ...(horizontal ? { x: target } : { y: target }) } : l)) };
}

/** Even spacing between the outermost selected pins. */
export function distribute(d: Draft, ids: string[], axis: "x" | "y"): Draft {
  const pins = d.lights.filter((l) => ids.includes(l.lightId)).sort((a, b) => a[axis] - b[axis]);
  if (pins.length < 3) return d;
  const first = pins[0][axis];
  const step = (pins[pins.length - 1][axis] - first) / (pins.length - 1);
  const pos = new Map(pins.map((p, i) => [p.lightId, first + step * i]));
  return { ...d, lights: d.lights.map((l) => (pos.has(l.lightId) ? { ...l, [axis]: pos.get(l.lightId)! } : l)) };
}
