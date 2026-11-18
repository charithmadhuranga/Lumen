---
name: lumen-webui
description: React WebUI work for Lumen — app shell and routing, auth screens, REST/Zod API client, TanStack Query + Zustand state split, WebSocket live store, lighting controls with throttling, scenes/rooms/devices screens, hardware configuration UI, themes, accessibility, bundle budgets, and the mock (MSW) API mode. Use for any change under webui/ or the firmware REST/WebSocket endpoints it consumes. Triggers: "webui", "react", "frontend", "ui screen", "api client", "zod schema", "websocket live state", "bundle budget", "phase 6", "phase 7".
---

# ESP Lighting — WebUI

**Plans:** `.opencode/plans/phase-06-webui-foundation.md`,
`phase-07-hardware-configuration-ui.md`.
**Spec:** `DESIGN.md` is authoritative for the WebUI (wins over SPEC §18);
SPEC §17 (API), §16.1–16.3 (auth).

## Before you start

1. Check the **Tailwind major version (v3 vs v4)** before following any
   setup snippet (DESIGN §2).
2. Read the DESIGN.md section for the screen you are touching (§7 screens,
   §8 data/state, §14 performance, §16 testing).

## State ownership (the rule that prevents most bugs)

| Data | Owner |
|---|---|
| REST resources (lights, rooms, scenes, boards…) | TanStack Query (`queryKeys.ts`) |
| WebSocket live state, connection status | `stores/liveStore` — which **patches the Query cache** via `queryClient.setQueryData` |
| Floor-plan / automation editor drafts, undo stacks | their Zustand stores |
| Theme, sidebar, last tabs | `uiStore` (localStorage, try/catch) |
| Form fields | React Hook Form + Zod resolver |

A piece of data has exactly one owner — never a second copy.

## API and WS

- `services/api.ts` is the only fetch: unwraps `{success, data, error}` once,
  throws typed `ApiError {code, message}`, `credentials: "same-origin"`,
  CSRF header on mutations, 401 → clear session → `/login`.
- Every response **and** every WS message is parsed with a Zod schema from
  `webui/src/schemas/`; unknown events/versions are ignored; parse failures
  surface as a generic toast. Zod schemas are the API type source of truth
  (`z.infer`) and change in the same commit as the firmware validator.
- `services/ws.ts`: single client on `/api/v1/ws`, exponential backoff
  reconnect (1 s → 30 s + jitter), refetch + resubscribe on reconnect,
  coalesce rapid events to one render per frame; diagnostics ≤1 Hz only while
  the tab is visible.

## Screens and components

- Lazy route per DESIGN §6.2; auth guard → `/login`; pending password change
  → `/setup/password` (blocks everything else). Never prefill credentials.
- Type-aware light controls (DESIGN §5.3) — **never render a control the
  light does not support**. Slider commands throttled: ≤1 per 100 ms while
  dragging + one final on release; local value while dragging, reconcile on
  the next `light_state_changed`.
- Hardware screens (Phase 7): all pin/GPIO data comes from the API — the
  frontend contains zero hardware knowledge (AGENTS.md §9). Secrets are
  write-only fields ("•••••• (set)"), never returned by the API.
- Loading → `Skeleton`; empty → `EmptyState`; API error → inline alert +
  Retry; offline → `ConnectionBanner` with controls disabled but visible;
  destructive actions → `ConfirmDialog`/`alert-dialog`.
- Toasts (Sonner): confirmations and errors only — never every light toggle;
  banners for persistent conditions.

## Look and motion

- Design tokens only — no hardcoded colors/radii/spacing (exception: user
  glow colors). Light/dark/system theme; status never color-only.
- `MotionConfig reducedMotion="user"`; animate `transform`/`opacity` only;
  `LazyMotion` + `m` components; named Lucide imports (no `import *`).
- 320 px → desktop, no horizontal scroll, ≥44 px touch targets.

## Bundle budgets (gzipped, DESIGN §14.1)

Initial JS ≤150 KB · CSS ≤25 KB · floor-plan viewer ≤40 KB · Konva editor
≤90 KB · React Flow ≤100 KB · Recharts ≤90 KB · GSAP ≤30 KB · total
LittleFS ≤1.0 MB. Konva, React Flow, Recharts, GSAP load only on their
routes; run the budget check before you finish.

## Validation gate

- [ ] `npm run lint && npm run typecheck && npm test && npm run build` green.
- [ ] Component tests for the controls/forms you touched; MSW flow test for
      new endpoints; Playwright + axe for new main screens (light + dark).
- [ ] Works in mock mode **and** against a real controller when available.
- [ ] Reduced motion + both themes spot-checked; bundle budgets pass.
- [ ] `docs/UI_UX.md` and `docs/API.md` updated for new screens/endpoints.

## Pitfalls

- Duplicating server data in Zustand "for convenience".
- Skipping Zod parsing on a response because the type "should" match.
- Loading a heavy library on the initial route.
- Hardcoded hex colors outside tokens.
- Storing tokens in localStorage (HTTP-only cookies only — DESIGN §13).
