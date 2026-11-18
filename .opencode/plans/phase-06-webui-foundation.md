# Phase 6 — WebUI Foundation

**SPEC §26 Phase 6, SPEC §16.1–16.3, §17, §19.4; DESIGN §1–8, §11–15, §17 steps 1–5** ·
**Skill:** `lumen-webui`

## Outcome

A working product surface: firmware serves the SPA from LittleFS, login with
forced password change works, REST + WebSocket are live and typed, and the
Overview / Rooms / Scenes / Devices screens control real lights in real time,
on desktop and mobile, light and dark.

## Dependencies

Phases 1 (core), 4–5 (lighting + scenes for data), 0 (scaffold).

## Tasks

### 6.1 Firmware web foundation (`src/web/`)

- [ ] `web_server.*`: ESP-IDF HTTP server; static file serving from LittleFS
      with pre-gzipped assets (`Content-Encoding: gzip`), immutable cache for
      hashed bundles, `no-cache` for `index.html`, SPA fallback to
      `index.html` for unknown non-API paths (DESIGN §14.2), strict CSP
      header (DESIGN §13), request-size limits.
- [ ] `auth_manager.*` (baseline security — hardened in Phase 12): default
      `admin`/`admin` provisioned in NVS with a **forced password change
      flag** on first login; PBKDF2-HMAC-SHA256 with per-user salt; secure
      random session tokens in HTTP-only, SameSite cookies; logout; session
      invalidation on password change; login rate limiting; CSRF token for
      mutating requests (SPEC §16).
- [ ] `api_routes.*`: response envelope `{success, data, error}` everywhere
      (SPEC §17.1). Implement the endpoint groups needed by this phase:
      **auth** (login/logout/session/password), **system** (info/health),
      **lights** (CRUD + `POST /lights/{id}/command`), **rooms**, **groups**,
      **scenes** (+ activate).
- [ ] Input validation on every endpoint (schema-level, bounded bodies);
      typed error codes (`INVALID_CONFIGURATION`, `UNAUTHORIZED`, …).
- [ ] `websocket_manager.*`: `/api/v1/ws`, versioned envelope
      `{version, event, timestamp, data}`, authenticated via the session
      cookie, bounded outbound queue, event fan-out from the bus
      (`light_state_changed`, `group_state_changed`, `scene_activated`,
      `system_status`, …), slow-client policy (drop coalescable telemetry,
      never block the bus consumer).
- [ ] Static asset pipeline: webui `build` → gzip → copy into the PlatformIO
      data directory (`data/` for `buildfs`); wire `pio run -t buildfs`.

### 6.2 Web UI application foundation (DESIGN §2–8)

- [ ] App shell: shadcn `sidebar` (Control/Configure groups + divider), top
      bar, mobile bottom nav (Overview, Rooms, Scenes, Floor Plans) + sheet,
      responsive from 320 px (DESIGN §6).
- [ ] Routes (DESIGN §6.2) with `React.lazy`, auth guard, and
      `/setup/password` guard that blocks everything until the default
      password is changed.
- [ ] `services/api.ts`: single typed fetch client, envelope unwrap, typed
      `ApiError`, 401 → redirect, Zod parsing of every response, CSRF header
      (DESIGN §8.1).
- [ ] `schemas/` Zod set mirroring the firmware validators (lights, rooms,
      groups, scenes, auth, system); `z.infer` types only — no hand-written
      duplicate types.
- [ ] `services/queries/` TanStack Query hooks + one `queryKeys.ts`;
      staleTime rules from DESIGN §8.3; mutations invalidate, light commands
      rely on WS confirmation.
- [ ] `services/ws.ts` + `stores/liveStore.ts`: reconnect with backoff
      (1 s → 30 s + jitter), Zod-validate each message, patch the Query cache
      via `setQueryData` (no second copy of server data), coalesce rapid
      events to one render per frame, `connectionStatus` →
      `ConnectionBanner` (DESIGN §8.4).
- [ ] Mock API mode: MSW handlers + mock WebSocket so the UI develops
      without hardware (DESIGN §14.4).
- [ ] Providers: QueryClient, `<Toaster />` (Sonner), theme
      (light/dark/system), `MotionConfig reducedMotion="user"`, route-level
      error boundaries.

### 6.3 Lighting components and first screens

- [ ] Components (DESIGN §5.2): `PowerToggle`, `BrightnessSlider`
      (throttled: ≤1 command/100 ms during drag + final on release, DESIGN
      §8.2), `ColorPicker` (pointer-event implementation, hex + RGB inputs),
      `CctSlider`, `EffectPicker`, `LightCard`, `RoomCard`, `SceneCard`,
      `StatusDot`, `ConnectionBanner`, `EmptyState`, `ConfirmDialog`.
- [ ] Type-aware control sets per DESIGN §5.3 — never render a control the
      light does not support.
- [ ] Screens: **Login + forced password change** (Zod: ≥10 chars, differs
      from `admin`, rate-limit countdown), **Overview** (DESIGN §7.2 order,
      no engineering metrics), **Rooms** grid + detail with aggregation
      policy behavior, **Scenes** grid with activate/duplicate/export,
      **Devices → Lights** table with add/edit dialog.
- [ ] Forms: React Hook Form + `zodResolver`, inline errors, pending state,
      unsaved-change warning (DESIGN §8.5).

### 6.4 Validation & a11y baseline

- [ ] Component tests (Vitest + RTL): controls per light type, throttling
      behavior, envelope error handling, auth guard redirects.
- [ ] MSW integration tests: login → session → light command → WS patch.
- [ ] Playwright: auth + forced password change, light control flow, mobile
      viewport; axe on every main route (light and dark).

## Contracts & interfaces introduced

- REST endpoints listed in SPEC §17.2 for auth/system/lights/rooms/groups/
  scenes — recorded in `docs/API.md` with example envelopes.
- WS event catalogue (SPEC §17.3) — recorded in `docs/API.md`.
- The Zod schema set becomes the API contract for all later UI phases.

## Gate (acceptance criteria)

1. Desktop and mobile UI function end to end against a real controller **and**
   against mock mode.
2. First login forces password change; session dies after change; unauthenticated
   API/WS access rejected.
3. Light state changes propagate over WS in <250 ms on a healthy LAN and
   reconcile slider drags (no fighting between local and remote values).
4. `lint`, `typecheck`, `test`, `build` pass; no serious/critical axe
   violations; both themes pass contrast spot-checks.
5. Bundle budgets (DESIGN §14.1) measured for the initial chunk — record the
   numbers; the CI budget script now enforces them.
6. UI works with the network cable pulled from the internet (no external
   requests — verify in devtools).

## Docs to update

`docs/API.md` (endpoints + WS events), `docs/UI_UX.md` (shell, routes,
states), `docs/IMPLEMENTATION_STATUS.md`, `AGENTS.md` §3 if commands changed.

## Risks / open questions

- ESP-IDF HTTP server connection limits drive chunk strategy — prefer fewer,
  larger chunks (DESIGN §14.2).
- Session + CSRF design here must anticipate Phase 12 hardening (rate limits,
  cookie flags audit) without rework — implement the flags correctly now.
