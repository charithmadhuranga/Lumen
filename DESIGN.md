# DESIGN.md — Lumen WebUI

**Scope:** Frontend design system and UI implementation specification
**Companion to:** `SPEC.md` (Product Requirements & Firmware Architecture)
**Audience:** Developers and coding agents implementing `webui/`
**Version:** 1.0

This document is authoritative for the WebUI. Where it conflicts with `spec.md` section 18, this document wins.

---

## 1. Design Principles

1. **Everyday first.** Turning lights on, dimming and activating scenes must take one or two taps. Engineering configuration lives behind Hardware, Integrations and Settings.
2. **Looks like a consumer lighting app, not a diagnostics console.** Calm, spacious, visual.
3. **Device is the source of truth.** The UI shows actual controller state pushed over WebSocket. It never simulates lighting state locally (optimistic updates are allowed only briefly and must reconcile).
4. **Edit and operate are separate.** Nothing in operating mode can accidentally move, delete or reconfigure.
5. **Light on flash.** The UI is served from ESP32 LittleFS. Every dependency must justify its bytes.
6. **Accessible.** WCAG 2.2 AA, keyboard operable, reduced-motion respected.
7. **Works offline.** No CDN, no web fonts from the internet, no external requests. Everything is self-hosted from the controller.

---

## 2. Technology Stack

| Concern | Technology | Notes |
|---|---|---|
| Framework | React + TypeScript + Vite | Strict TS |
| UI components | shadcn/ui | Radix primitives, copied into `src/components/ui` |
| CSS | Tailwind CSS | Tokens as CSS variables |
| Animation | Motion for React | `import { motion } from "motion/react"` |
| Advanced sequences | GSAP (optional) | Lazy-loaded; never on the critical path |
| Floor-plan editor | React Konva | Edit mode only |
| Live floor plan | Inline SVG + Motion | Control mode |
| Automation editor | React Flow | `@xyflow/react`, advanced tab only |
| Icons | Lucide React | Named imports only (tree-shaken) |
| Client/live state | Zustand | |
| Server state | TanStack Query | |
| Forms | React Hook Form | with `@hookform/resolvers/zod` |
| Validation | Zod | Source of truth for API types |
| Charts | Recharts | Via shadcn `chart` wrapper, Diagnostics only |
| Toasts | Sonner | shadcn `sonner` component |
| Routing | React Router | Lazy routes |

Version policy: pin exact versions via lockfile. Check the Tailwind major version (v3 vs v4) and the shadcn CLI output before scaffolding, and follow the matching setup. Do not upgrade mid-project.

### 2.1 Responsibility split for state

| Data | Owner |
|---|---|
| Lights, rooms, groups, scenes, automations, boards (REST resources) | TanStack Query |
| Live light state, input events, controller status (WebSocket) | Zustand `liveStore`, which also patches the Query cache |
| Floor-plan editor draft, selection, undo stack | Zustand `floorplanEditorStore` |
| Automation graph draft | Zustand `automationEditorStore` |
| Theme, sidebar, last-used tabs | Zustand `uiStore` (persisted to `localStorage`, wrapped in try/catch) |
| Form field state | React Hook Form |

Rule: a piece of data has exactly one owner. WebSocket events update the Query cache with `queryClient.setQueryData` rather than creating a second copy.

---

## 3. Project Structure

```text
webui/
├── components.json                # shadcn/ui config
├── package.json
├── vite.config.ts
├── tailwind.config.ts             # only if on Tailwind v3
└── src/
    ├── app/
    │   ├── App.tsx
    │   ├── router.tsx
    │   ├── providers.tsx          # QueryClient, Toaster, Theme
    │   └── layout/
    │       ├── AppShell.tsx
    │       ├── Sidebar.tsx
    │       ├── TopBar.tsx
    │       └── BottomNav.tsx
    ├── components/
    │   ├── ui/                    # shadcn primitives (button, card, dialog, ...)
    │   └── lighting/
    │       ├── LightCard.tsx
    │       ├── PowerToggle.tsx
    │       ├── BrightnessSlider.tsx
    │       ├── ColorPicker.tsx
    │       ├── CctSlider.tsx
    │       ├── EffectPicker.tsx
    │       ├── SceneCard.tsx
    │       └── StatusDot.tsx
    ├── features/
    │   ├── auth/
    │   ├── overview/
    │   ├── rooms/
    │   ├── floorplan/
    │   │   ├── FloorPlanViewer.tsx      # SVG + Motion (control mode)
    │   │   ├── FloorPlanEditor.tsx      # React Konva (edit mode)
    │   │   ├── LightGlow.tsx
    │   │   ├── sanitizeSvg.ts
    │   │   └── coords.ts
    │   ├── scenes/
    │   ├── automations/
    │   │   ├── SimpleEditor.tsx
    │   │   ├── flow/                    # React Flow nodes, edges, validation
    │   │   └── schema.ts
    │   ├── devices/
    │   ├── hardware/
    │   ├── integrations/
    │   ├── network/
    │   └── settings/
    ├── hooks/
    ├── lib/
    │   ├── utils.ts                     # cn()
    │   └── motion.ts                    # shared transitions and variants
    ├── schemas/                         # Zod schemas mirroring firmware API
    ├── services/
    │   ├── api.ts                       # typed fetch client + envelope handling
    │   ├── ws.ts                        # WebSocket client
    │   └── queries/                     # TanStack Query hooks per resource
    ├── stores/
    ├── types/
    └── styles/
        └── globals.css                  # tokens + Tailwind entry
```

Feature folders own their screens, hooks and local components. Shared UI goes in `components/`. Features must not import from other features; lift shared code into `components/`, `hooks/` or `lib/`.

---

## 4. Design Tokens

Tokens follow the shadcn/ui convention so every shadcn component themes automatically. Colors are defined as CSS variables, mapped into Tailwind, and never hardcoded in components.

### 4.1 Color tokens

```css
:root {
  --background: oklch(0.985 0.003 90);
  --foreground: oklch(0.21 0.01 260);

  --card: oklch(1 0 0);
  --card-foreground: var(--foreground);
  --popover: oklch(1 0 0);
  --popover-foreground: var(--foreground);

  --primary: oklch(0.72 0.16 65);              /* warm amber */
  --primary-foreground: oklch(0.2 0.02 65);
  --secondary: oklch(0.96 0.005 90);
  --secondary-foreground: var(--foreground);

  --muted: oklch(0.96 0.004 90);
  --muted-foreground: oklch(0.5 0.01 260);
  --accent: oklch(0.95 0.02 75);
  --accent-foreground: var(--foreground);

  --border: oklch(0.91 0.005 90);
  --input: oklch(0.91 0.005 90);
  --ring: oklch(0.72 0.16 65);

  --destructive: oklch(0.58 0.22 27);
  --success: oklch(0.65 0.16 150);
  --warning: oklch(0.78 0.15 80);
  --info: oklch(0.62 0.13 245);

  /* Lighting-specific */
  --light-on-glow: #ffd68a;                    /* default glow, per-light override allowed */
  --light-off: oklch(0.8 0.005 260);
  --floor-bg: oklch(0.97 0.004 90);

  /* Charts (categorical, colorblind-safe) */
  --chart-1: oklch(0.72 0.16 65);
  --chart-2: oklch(0.62 0.13 245);
  --chart-3: oklch(0.65 0.16 150);
  --chart-4: oklch(0.6 0.18 320);
  --chart-5: oklch(0.58 0.22 27);

  --radius: 0.625rem;
}

.dark {
  --background: oklch(0.16 0.01 260);
  --foreground: oklch(0.96 0.004 90);

  --card: oklch(0.2 0.012 260);
  --card-foreground: var(--foreground);
  --popover: oklch(0.2 0.012 260);
  --popover-foreground: var(--foreground);

  --primary: oklch(0.78 0.15 70);
  --primary-foreground: oklch(0.2 0.02 65);
  --secondary: oklch(0.25 0.012 260);
  --secondary-foreground: var(--foreground);

  --muted: oklch(0.25 0.012 260);
  --muted-foreground: oklch(0.7 0.01 260);
  --accent: oklch(0.28 0.03 70);
  --accent-foreground: var(--foreground);

  --border: oklch(0.3 0.012 260);
  --input: oklch(0.3 0.012 260);
  --ring: oklch(0.78 0.15 70);

  --destructive: oklch(0.65 0.2 25);
  --success: oklch(0.72 0.16 150);
  --warning: oklch(0.82 0.14 85);
  --info: oklch(0.7 0.12 245);

  --light-off: oklch(0.38 0.01 260);
  --floor-bg: oklch(0.18 0.012 260);
}
```

Theme rules:

- Light, dark and system modes. Default is system.
- Toggle adds or removes the `dark` class on `<html>`.
- Check contrast for text and interactive elements against WCAG AA in both themes.
- Status must never be conveyed by color alone; pair with an icon or label.
- Glow colors on the floor plan are user data, not theme tokens. Clamp their perceived brightness so a glow remains visible on both light and dark floor backgrounds.

### 4.2 Radius, spacing, motion

```css
:root {
  --radius-sm: calc(var(--radius) - 4px);   /* 6px  */
  --radius-md: calc(var(--radius) - 2px);   /* 8px  */
  --radius-lg: var(--radius);               /* 10px */
  --radius-xl: calc(var(--radius) + 6px);   /* 16px */

  --duration-fast: 150ms;
  --duration-normal: 250ms;
  --duration-slow: 400ms;
  --ease-out: cubic-bezier(0.22, 1, 0.36, 1);
}
```

Spacing uses the Tailwind 4px scale (`1`=4px, `2`=8px, `3`=12px, `4`=16px, `6`=24px, `8`=32px). Prefer `gap-*` over margins. Page gutters: 16px mobile, 24px tablet, 32px desktop.

### 4.3 Typography

- Font: system UI stack by default. If a custom font is required, self-host a single variable font subset (Latin, `woff2`) and keep it under 60 KB.
- Never load fonts from Google Fonts or any CDN.
- Scale: `text-xs` (12) meta, `text-sm` (14) body and controls, `text-base` (16) default, `text-lg` (18) card titles, `text-2xl` (24) page titles, `text-4xl` (36) hero values such as brightness percent.
- Numbers that change live (brightness %, color temperature) use `tabular-nums`.

### 4.4 Elevation

Minimal. Use borders first, `shadow-sm` for floating elements (popovers, sticky bars), `shadow-lg` only for dialogs. Avoid stacked cards inside cards.

---

## 5. Component System

### 5.1 shadcn/ui components to install

Install only what is used.

`button`, `card`, `input`, `label`, `textarea`, `select`, `checkbox`, `switch`, `slider`, `tabs`, `dialog`, `alert-dialog`, `sheet`, `dropdown-menu`, `popover`, `tooltip`, `badge`, `separator`, `scroll-area`, `skeleton`, `form`, `table`, `toggle`, `toggle-group`, `command`, `collapsible`, `progress`, `chart`, `sonner`, `sidebar`.

### 5.2 Custom lighting components

| Component | Purpose | Notes |
|---|---|---|
| `PowerToggle` | On/off | Large touch target (min 44×44px), shows pending state |
| `BrightnessSlider` | 0–100% | Wraps shadcn `Slider`; throttled commands (see 8.2) |
| `ColorPicker` | RGB / RGBW color | Hue ring + saturation/value area, hex input; no heavy dependency, implement with pointer events |
| `CctSlider` | Warm to cool | Gradient track from warm amber to cool blue, kelvin readout |
| `EffectPicker` | Effect + speed | Select with preview icon, speed slider |
| `LightCard` | One light | Icon, name, power, brightness; expands for type-specific controls |
| `RoomCard` | One room | Name, icon, master power, light count, scene chips |
| `SceneCard` | One scene | Icon, name, activate button, edit menu |
| `StatusDot` | Online/offline/error | Color + icon + `aria-label` |
| `ConnectionBanner` | WebSocket or controller offline | Non-blocking top banner |
| `EmptyState` | No data | Icon, one-line explanation, one action |
| `ConfirmDialog` | Destructive actions | Built on `alert-dialog` |
| `PinChip` | GPIO assignment | Shows pin number, role and conflict state |

### 5.3 Light controls by type

Controls adapt to the logical light type from `spec.md` section 8.2.

| Type | Controls shown |
|---|---|
| `switch` | Power |
| `dimmer` | Power, brightness |
| `rgb` | Power, brightness, color picker |
| `rgbw` | Power, brightness, color picker, white channel |
| `cct` | Power, brightness, warm/cool slider |
| `addressable_rgb` | Power, brightness, color, effect, effect speed |
| `addressable_rgbw` | Power, brightness, color, white channel, effect, effect speed |

Never render a control for a property the light does not support.

### 5.4 Component rules

- Functional components with typed props. No `any`.
- Use `cn()` for class composition.
- Variants via `class-variance-authority` as in shadcn.
- Do not edit generated shadcn files unless necessary; extend via wrappers. If you do edit, note it in a comment.
- Every interactive element has a visible focus ring (`--ring`).
- Icon-only buttons require `aria-label` and a tooltip.

---

## 6. Layout and Navigation

### 6.1 App shell

Desktop (≥1024px): shadcn `sidebar` (collapsible to icons) + top bar + main content.

```text
┌──────────────────────────────────────────────────────────┐
│ ☀ Logo     Controller name        ● Online     Profile ▾ │
├────────────┬─────────────────────────────────────────────┤
│ Overview   │                                             │
│ Rooms      │                                             │
│ Floor Plans│                MAIN CONTENT                 │
│ Scenes     │                                             │
│ Automations│                                             │
│ Devices    │                                             │
│ ─────────  │                                             │
│ Hardware   │                                             │
│ Integrations│                                            │
│ Network    │                                             │
│ Settings   │                                             │
└────────────┴─────────────────────────────────────────────┘
```

The nav is split into two groups separated by a divider: **Control** (Overview, Rooms, Floor Plans, Scenes, Automations, Devices) and **Configure** (Hardware, Integrations, Network, Settings).

Tablet (640–1023px): icon-only sidebar rail.

Mobile (<640px): top bar with a hamburger that opens a `sheet`; bottom navigation with four items — Overview, Rooms, Scenes, Floor Plans. Everything else is reachable from the sheet.

### 6.2 Routes

| Route | Screen |
|---|---|
| `/login` | Sign in |
| `/setup/password` | Forced password change |
| `/` | Overview |
| `/rooms`, `/rooms/:id` | Rooms |
| `/floorplans`, `/floorplans/:id` | Floor plan viewer; `?mode=edit` for editor |
| `/scenes`, `/scenes/:id` | Scenes |
| `/automations`, `/automations/:id` | Automations (Simple / Advanced tabs) |
| `/devices` | Lights, inputs, controllers |
| `/hardware/*` | Board, Pin Mapping, Peripherals, Outputs, Inputs, Diagnostics |
| `/integrations/*` | MQTT, Home Assistant, ESP-NOW, TCP/UDP |
| `/network` | Wi-Fi, AP, Ethernet |
| `/settings` | Account, theme, backup, OTA, system |

Routes are lazy-loaded with `React.lazy`. Auth guard redirects unauthenticated users to `/login` and users with a pending password change to `/setup/password`.

### 6.3 Responsive rules

- Mobile-first Tailwind (`sm:`, `md:`, `lg:`).
- No horizontal page scroll at 320px width.
- Touch targets at least 44×44px.
- Sliders have at least 40px of vertical hit area.
- Floor plan fills available width; controls move into a bottom sheet on mobile.
- Tables collapse into stacked cards on mobile.

---

## 7. Screen Specifications

### 7.1 Login and first-run

- Centered card, logo, username, password, submit.
- On first login with default credentials, route to `/setup/password`. Block all other routes until changed.
- Password form: new password, confirm, strength meter. Zod: min 10 chars, must differ from `admin`.
- Never prefill credentials. Rate-limit errors show a countdown.

### 7.2 Overview

Order of content:

1. Greeting with controller name and connectivity (`StatusDot`).
2. **Lights on** summary: "5 of 12 lights on" with an "All off" action.
3. **Rooms** grid: `RoomCard` with master toggle and brightness.
4. **Scenes** row: recently used, horizontal scroll with snap on mobile.
5. **Alerts**: only shown if present (offline peer, I2C error, OTA available).
6. **Active automations**: compact list with toggles.

Do not show heap, CPU or other engineering metrics here.

### 7.3 Rooms

- Grid of `RoomCard`s. Room detail shows master power and brightness, individual `LightCard`s, scene shortcuts, and an "Edit room" action.
- Room master brightness uses the aggregation policy from `spec.md` section 9.2 (adjust brightness, preserve each light's color).
- Create/edit room dialog uses React Hook Form + Zod: name, floor, icon, lights.

### 7.4 Scenes

- Visual card grid. Card: icon or tinted gradient derived from the scene's dominant color, name, **Activate** button, overflow menu (Edit, Duplicate, Export, Delete).
- "Save current state as scene" is the primary action.
- Scene editor: list of lights with per-light state (power, brightness, color), transition duration slider, live **Preview** and **Revert** buttons.
- Import/export JSON with Zod validation and a diff preview before import.

### 7.5 Floor plans

See section 9.

### 7.6 Automations

Two tabs: **Simple** and **Advanced**.

- **Simple** (`react-hook-form` + Zod): stepper-like form with four sections — Trigger, Conditions (optional), Actions, Execution settings. Each section uses a `select` for type and renders type-specific fields. Includes a plain-language summary at the top ("When input 1 turns on, between 18:00 and 23:00, turn on Kitchen Main at 80%").
- **Advanced**: React Flow editor, see section 10.
- A simple automation can be opened in Advanced. An advanced graph that cannot be represented as a simple form opens only in Advanced, with a notice.
- History drawer shows recent executions with result, duration and error.
- **Test** button runs the automation once and reports via toast.

### 7.7 Devices

Tabs: Lights, Inputs, Controllers.

- Lights: table of logical channels with type, driver, room, status. Add/edit dialog selects driver (from hardware outputs) and light type.
- Inputs: live input state with press/double/long indicators.
- Controllers: peers discovered via ESP-NOW/MQTT, role, last seen, link quality.

### 7.8 Hardware

Tabs: **Board**, **Pin Mapping**, **Peripherals**, **Outputs**, **Inputs**, **Diagnostics**.

- **Board:** active profile card, profile list (built-in locked with a lock icon), Duplicate / Import / Export / Restore factory.
- **Pin Mapping:** graphical GPIO grid for the selected MCU. Each pin is a `PinChip` colored by role with conflict, strapping and input-only indicators. Click to assign. Validation errors from `POST /boards/validate` appear inline next to the offending pin.
- **Peripherals:** add/edit PCA9685, PCF8574, relay, Ethernet, RS485, addressable LED. I2C scan button calls `GET /hardware/i2c/scan` and highlights unexpected addresses (see the PCA9685 `0x00` verification note in `spec.md` 6.3).
- **Outputs / Inputs:** table of hardware outputs and inputs with live state and test buttons.
- **Diagnostics:** free heap, task stats, I2C health, recent errors. Recharts line/area charts, polled or streamed at a low rate (see 8.4).
- **Apply workflow:** Validate → review summary → confirm dialog → apply with progress → success or rollback result. On failure show the rollback result clearly. A profile change is dangerous: it needs an `alert-dialog` stating that outputs will go to safe states.

### 7.9 Integrations

Cards for MQTT, Home Assistant, ESP-NOW, TCP, UDP. Each has an enable switch, settings form and a **Test connection** button where applicable. Secrets are write-only fields: show "•••••• (set)" and a Replace action; never return secrets from the API.

### 7.10 Network

Wi-Fi (scan, SSID, password, DHCP/static), AP, Ethernet, status. Changing network settings that can disconnect the UI requires a confirm dialog explaining that the connection may drop and how to reconnect.

### 7.11 Settings

Account (change password, session timeout), Appearance (theme), Backup & restore, OTA update (upload with progress and validation result), System (restart, factory reset behind a typed-confirmation dialog), About (version, build).

---

## 8. Data, State and Realtime

### 8.1 API client

- Single typed client in `services/api.ts`.
- Unwraps the envelope `{ success, data, error }` from `spec.md` 17.1. On `success: false` it throws a typed `ApiError { code, message }`.
- Sends cookies with `credentials: "same-origin"` and the CSRF header on mutating requests.
- On 401, clears session state and redirects to `/login`.
- Every response is parsed with the matching Zod schema. Parse failures are logged and surfaced as a generic error toast.

### 8.2 Command throttling

Slider drags produce many values. Rules:

- Send at most one `command` per 100 ms while dragging (throttle, trailing edge) and always one final command on release.
- Include a short `transition_ms` for slider commands; use 0 for scrubbing.
- A newer command replaces an in-flight one (matches `spec.md` 8.5).
- Show the local value immediately while dragging. After release, reconcile with the next `light_state_changed` event.

### 8.3 TanStack Query

- Query keys: `['lights']`, `['lights', id]`, `['rooms']`, `['scenes']`, `['automations']`, `['boards']`, etc., defined in one `queryKeys.ts`.
- `staleTime` of 30 s for configuration resources; `Infinity` for live-state queries kept fresh by WebSocket patches.
- Mutations invalidate affected keys. Light commands use no invalidation, they rely on WebSocket confirmation.
- Retry: 1 for GETs, 0 for mutations.
- Disable `refetchOnWindowFocus` for lighting state to avoid redundant ESP32 load; keep it for system status.

### 8.4 WebSocket

- Single client in `services/ws.ts` connecting to `/api/v1/ws`.
- Handles the versioned envelope `{ version, event, timestamp, data }`; ignore unknown versions and events.
- Validate each message with Zod; drop invalid ones.
- Exponential backoff reconnect (1 s → 30 s cap) with jitter. On reconnect, refetch light state and resubscribe.
- Expose `connectionStatus` in `liveStore`; `ConnectionBanner` reacts to it.
- Coalesce rapid events (e.g. brightness changes) into one render per animation frame.
- Poll or stream diagnostics at no more than 1 Hz and only while the Diagnostics tab is visible.

### 8.5 Forms

- React Hook Form with `zodResolver`. Schemas live in `schemas/` and are shared with API parsing.
- Show inline field errors; map server `INVALID_CONFIGURATION` errors onto fields when possible, otherwise show a form-level alert.
- Disable submit while pending; warn on navigation with unsaved changes.

### 8.6 Notifications (Sonner)

- `<Toaster />` mounted once in `providers.tsx`, position bottom-center on mobile, bottom-right on desktop.
- Success toasts: 2.5 s. Errors: persist until dismissed. Include an action when useful (Retry, Undo).
- Use toasts for confirmations and errors. Use banners for persistent conditions (offline). Do not toast every light toggle.

---

## 9. Floor Plan System

The floor plan has two renderers sharing one coordinate system.

### 9.1 Coordinate system

- All stored positions use the SVG `viewBox` coordinate space from the floor-plan JSON (`spec.md` 10.4), not screen pixels.
- `coords.ts` provides `screenToSvg`, `svgToScreen` and snap-to-grid helpers used by both renderers.
- The uploaded SVG is never modified to hold lighting data (`spec.md` 10.3).

### 9.2 SVG sanitization

Run on upload (client pre-check) and again before render. The firmware must also reject unsafe SVG.

- Parse with `DOMParser` as `image/svg+xml`.
- Remove `<script>`, `<foreignObject>`, `<iframe>`, `<object>`, `<embed>`, `<use>` pointing to external resources, `<image>` with external `href`, `<style>` with `@import` or `url()` to external hosts.
- Strip all `on*` attributes and `javascript:` / external URLs in `href`, `xlink:href`, `style`.
- Enforce a size limit (e.g. 512 KB) and an element-count limit.
- Prefer a maintained sanitizer such as DOMPurify configured for SVG; add a project test suite with malicious sample files.

### 9.3 Control mode (SVG + Motion)

- Render the sanitized SVG inline inside a responsive container. Overlay layers are sibling `<g>` elements in a single `<svg>` with the same `viewBox`:
  1. Original SVG content
  2. Room overlay (hit areas, labels)
  3. Lighting overlay (glows, icons)
  4. Sensor overlay
  5. Interaction layer
- **Light glow:** SVG `<radialGradient>` circle per light, color from state (or `glow_color`), opacity and radius driven by brightness. Use `<feGaussianBlur>` sparingly (one shared filter, not per light) to keep the GPU load low on phones.
- **Animations with Motion:**
  - Power on/off: `animate` opacity and scale of the glow, 250 ms ease-out.
  - Brightness: map 0–100 to opacity 0.15–1 and radius 60–100% of `glow_radius`.
  - Color: animate the gradient stop color with Motion's color interpolation.
  - Hover/press: icon scale 1.08 / 0.96 spring.
  - Room selection: outline + subtle tint.
- Animate only `opacity`, `transform` and gradient stop values. Do not animate filter parameters or the full-floor background.
- Tap a light icon to toggle. Long-press or tap again opens the light control popover (mobile: bottom sheet). Tap a room to select and show room controls.
- Zoom and pan: pinch, wheel, drag, and +/− / reset buttons. Implement with a transform on the root `<g>` driven by a small pan/zoom hook; clamp the range (e.g. 0.5×–6×). Keyboard accessible.
- Fullscreen via the Fullscreen API with an `Esc` hint.
- Use actual device state from `liveStore`. Never keep a separate simulated state.
- Respect `prefers-reduced-motion`: cut transitions to instant opacity changes.

### 9.4 Edit mode (React Konva)

Used only for placement and layout.

- `Stage` + `Layer`s: background layer shows the sanitized SVG rasterized as a non-listening `Konva.Image` (load the sanitized SVG via a Blob URL as an `<img>`; scripts do not run in image context). Objects layer holds draggable light nodes.
- Stage size matches the container; the viewBox maps to the stage via a single scale and offset so coordinates convert through `coords.ts`.
- Features: drag lights, select, multi-select, `Transformer` for glow-radius handle, nudge with arrow keys, snap to grid (toggle), align and distribute, delete, undo/redo (history stack in `floorplanEditorStore`), zoom and pan.
- Side panel (shadcn `sheet`/panel): assign a logical light (searchable `command` list of unplaced lights), icon, glow radius, glow color. Unassigned lights tray lets users drag a light onto the plan.
- Autosave draft to the store; **Save** posts to `PUT /floorplans/{id}/layout`; **Cancel** discards. Warn on unsaved exit.
- Room regions: draw polygons or rectangles to define room hit areas (stored as overlay data, not in the SVG).
- Konva is lazy-loaded; control mode must not import it.

### 9.5 Mode switching

- Segmented toggle: **Control** | **Edit**, placed in the floor-plan toolbar.
- Edit mode shows a persistent "Editing" badge and a tinted border so it cannot be mistaken for control mode.
- Edit mode is entered only through the toggle. Deep links default to control mode.
- Switching with unsaved changes prompts Save / Discard / Cancel.

### 9.6 Optional GSAP

GSAP is permitted only for sequences that Motion handles poorly, such as a synchronized scene-activation ripple across many lights or a timeline-driven sunrise preview. Load it via dynamic import on first use. Core control must work without it.

---

## 10. Automation Editor (React Flow)

- Lazy-load the Advanced tab. Import React Flow styles only there.
- **Node types** (custom nodes, built with shadcn `Card` styling): Trigger, Condition, AND, OR, Delay, Light action, Group action, Scene action, Relay action, MQTT action, UDP action, TCP action, Variable, Automation execution.
- Each node has a colored header by category (trigger, logic, action), icon (Lucide), title and inline fields with the same Zod validation as the Simple editor.
- **Palette:** left panel with searchable node list (`command`) and drag-to-canvas; also an "Add node" popover opened with `/` or double-click on the canvas.
- **Connection validation:** use `isValidConnection`. Rules include one trigger per graph, triggers have no inputs, actions cannot feed triggers, no cycles (detect with DFS), type-compatible handles, and no orphan action nodes at save time.
- Invalid connections show a red handle highlight and a toast on drop.
- Controls: minimap, zoom controls, fit view, snap to grid, undo/redo, auto layout (simple left-to-right layered layout).
- Serialize to the versioned automation graph schema (`version`, `nodes`, `edges`). Never store functions or code; only declarative data. The engine never executes user JavaScript (`spec.md` 11.7).
- Mobile: the canvas is view/pan-only with a notice; full editing is desktop and tablet. The Simple editor remains fully usable on mobile.

---

## 11. Motion and Animation

### 11.1 Principles

Subtle, fast, purposeful. Motion explains state change; it never decorates.

### 11.2 Shared motion tokens (`lib/motion.ts`)

```ts
export const transition = {
  fast:   { duration: 0.15, ease: [0.22, 1, 0.36, 1] },
  normal: { duration: 0.25, ease: [0.22, 1, 0.36, 1] },
  slow:   { duration: 0.4,  ease: [0.22, 1, 0.36, 1] },
  spring: { type: "spring", stiffness: 400, damping: 30 },
} as const;

export const fadeUp = {
  initial: { opacity: 0, y: 8 },
  animate: { opacity: 1, y: 0, transition: transition.normal },
  exit:    { opacity: 0, y: 8, transition: transition.fast },
};
```

### 11.3 Where motion is used

| Element | Motion |
|---|---|
| Page transitions | Fade-up, 250 ms, no layout shift |
| Cards entering | Staggered fade-up, 30 ms stagger, max 8 items |
| Power toggle | Spring thumb, glow pulse on turn-on |
| `LightCard` expand | `layout` animation, height auto |
| Dialogs and sheets | shadcn defaults |
| Floor-plan glows | See 9.3 |
| Scene activation | Brief pulse on the card; optional GSAP ripple on the floor plan |
| Lists reorder | Motion `Reorder` or `layout` |

### 11.4 Rules

- Honor `prefers-reduced-motion` globally with Motion's `MotionConfig reducedMotion="user"`.
- Animate `transform` and `opacity` only. Avoid animating `width`, `height` or `box-shadow` on large lists.
- Use `LazyMotion` with `domAnimation` (and `m` components) to keep the bundle small. Load `domMax` only if drag or layout features need it.
- Do not animate on every live state tick for lists of more than ~30 items; batch updates.
- Keep CPU in mind: pause all decorative animation when the tab is hidden.

---

## 12. Accessibility

- Target WCAG 2.2 AA.
- Radix/shadcn provides focus management for dialogs, menus and popovers; do not override it.
- All controls keyboard operable. Sliders support arrow keys, Page Up/Down, Home/End.
- Floor-plan lights are focusable (`tabIndex=0`, `role="switch"`, `aria-checked`, `aria-label` with light and room name). Provide an equivalent list view of the same lights for screen reader and keyboard users.
- Color picker has hex and numeric RGB inputs as alternatives.
- Live state changes announce via a polite `aria-live` region for the user's own actions only, not for every remote event.
- Visible focus indicators; minimum 3:1 contrast for UI boundaries.
- Do not use color alone for status.
- Run axe checks in Playwright on every main screen.

---

## 13. Security in the UI

- Treat all data from the controller as untrusted when rendering. React escapes by default; never use `dangerouslySetInnerHTML` except for the sanitized SVG, and only through the single `SanitizedSvg` component.
- Do not store tokens in `localStorage`. Authentication uses HTTP-only cookies; `localStorage` is only for non-sensitive UI preferences.
- Send the CSRF token on state-changing requests.
- Secrets are never displayed or returned by the API; fields are write-only.
- Redact secrets in any client-side error logging.
- Confirm destructive actions (delete, factory reset, apply board profile, restore backup).
- Serve with a strict CSP from firmware: `default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; connect-src 'self' ws: wss:`. Avoid inline scripts. Tailwind and shadcn work without them. (Konva's blob/data image loading is why `blob:` is permitted for images.)

---

## 14. Performance and Build for ESP32

The UI is served from limited flash over a modest Wi-Fi link, so size and request count matter.

### 14.1 Budgets (gzipped)

| Asset | Budget |
|---|---|
| Initial JS (shell, auth, overview) | ≤ 150 KB |
| CSS | ≤ 25 KB |
| Floor-plan viewer chunk | ≤ 40 KB |
| Floor-plan editor chunk (Konva) | ≤ 90 KB |
| Automation editor chunk (React Flow) | ≤ 100 KB |
| Diagnostics chunk (Recharts) | ≤ 90 KB |
| GSAP (if used) | ≤ 30 KB |
| Total LittleFS web partition | ≤ 1.0 MB |

Treat these as starting targets; measure with the real build and set them in CI. Fail the build when exceeded.

### 14.2 Techniques

- Route-level code splitting with `React.lazy` and Vite manualChunks (vendor-react, vendor-query, vendor-radix, and one chunk each for konva, reactflow, recharts).
- Import icons by name from `lucide-react`; never `import *`.
- Import Recharts and Motion in the smallest form (`LazyMotion`, per-chart imports).
- Pre-compress all assets with gzip in the build step (a Vite plugin or post-build script). The firmware serves `file.js.gz` with `Content-Encoding: gzip` and `Cache-Control: public, max-age=31536000, immutable` for hashed files; `index.html` is `no-cache`.
- Limit simultaneous requests; ESP-IDF HTTP server has a small connection limit. Prefer fewer, larger chunks over many tiny ones. Preload only the critical chunk.
- No source maps in the firmware image.
- Remove unused Tailwind classes (default) and unused shadcn components.
- Vite `base: "/"`; client-side routing needs the firmware to return `index.html` for unknown non-API paths.

### 14.3 Runtime performance

- Virtualize lists only when there are more than ~100 rows (unlikely for lighting; avoid premature complexity).
- Memoize `LightCard` and select from Zustand with narrow selectors so one light update does not re-render all cards.
- Debounce search inputs; throttle slider commands (8.2).
- Show `Skeleton` placeholders while loading; avoid layout shift.

### 14.4 Development workflow

- `npm run dev` proxies `/api` and `/api/v1/ws` to the controller IP via Vite `server.proxy`, configurable by env var.
- Provide a **mock API mode** (MSW + a mock WebSocket) so the UI can be developed without hardware.
- `npm run build` → `dist/` → gzip → copied to the PlatformIO LittleFS data directory.

---

## 15. Error, Empty and Loading States

| State | Treatment |
|---|---|
| Loading | `Skeleton` matching the final layout |
| Empty | `EmptyState`: icon, one sentence, one primary action |
| API error | Inline alert with message from `error.message` and Retry |
| Field error | Inline under the field |
| Controller offline | `ConnectionBanner`, controls disabled but visible |
| Partial failure | Toast with action and details link |
| Unsupported capability | Hide the control; in Hardware show "Not supported on this board" |
| Pending command | Subtle spinner on the control; roll back if no confirmation within 3 s and toast an error |

Error boundaries wrap each route and the floor-plan canvas so a failure in Konva or React Flow does not blank the whole app.

---

## 16. Testing

| Layer | Tooling | Covers |
|---|---|---|
| Unit | Vitest | Zod schemas, `coords.ts`, SVG sanitizer, throttling, graph validation |
| Component | Vitest + React Testing Library | Light controls per type, forms, banners |
| API mocking | MSW | REST and WebSocket flows |
| E2E | Playwright | Auth + forced password change, light control, scenes, floor-plan edit and control, automation simple and advanced, hardware apply and rollback, mobile viewport |
| Accessibility | axe + Playwright | Every main route in light and dark |
| Visual (optional) | Playwright screenshots | Floor-plan glow rendering, theme parity |
| Bundle size | CI script | Budgets in section 14 |

Mandatory sanitizer tests include SVGs containing `<script>`, `onload`, `javascript:` hrefs, external `<image>`, `<foreignObject>` and `@import` styles.

---

## 17. Implementation Order

1. Scaffold Vite + React + TS, Tailwind, shadcn init, tokens, theme toggle, Sonner.
2. App shell, routing, auth flow with forced password change, API client and Zod schemas.
3. WebSocket client, `liveStore`, Query integration, mock API mode.
4. Lighting components and Rooms/Overview.
5. Scenes.
6. Floor-plan viewer (SVG + Motion) with sanitizer.
7. Floor-plan editor (React Konva).
8. Automations: Simple editor, then React Flow editor.
9. Hardware screens with validation and apply workflow.
10. Integrations, Network, Settings, OTA.
11. Diagnostics (Recharts).
12. Accessibility pass, bundle budgets, E2E suite.

Each step ships with tests. Do not start a later step's heavy dependency before the earlier step is working.

---

## 18. Definition of Done for the WebUI

- All screens in section 7 implemented and responsive from 320px to desktop.
- Light and dark themes pass contrast checks.
- Control and edit modes are separated and cannot be confused.
- No hardware or GPIO logic outside the Hardware feature, and even there only as data from the API.
- No hardcoded colors outside tokens and user-defined glow colors.
- Heavy libraries are lazy-loaded; bundle budgets pass.
- The UI works with no internet access.
- Sanitizer, graph validation and throttling tests pass.
- Axe reports no serious or critical violations.
- Works against both the mock API and a real controller.
