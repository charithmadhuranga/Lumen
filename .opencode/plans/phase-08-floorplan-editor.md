# Phase 8 — Floor-Plan Editor and Viewer

**SPEC §26 Phase 8, SPEC §10; DESIGN §9** · **Skill:** `lumen-floorplan`

## Outcome

Users upload an SVG, lay out rooms and lights, and switch to a control mode
where the floor plan becomes the lighting interface — glows driven by actual
device state. The uploaded SVG is never modified; sanitization is enforced
client- and firmware-side.

## Dependencies

Phase 5 (rooms/lights to place), Phase 6 (web stack + WS live state).

## Tasks

### 8.1 Firmware floor-plan endpoints and storage

- [ ] `GET/POST/PUT/DELETE /floorplans`, `POST /floorplans/{id}/upload`
      (SVG), `PUT /floorplans/{id}/layout` (SPEC §17.2).
- [ ] Upload validation server-side: size limit (512 KB), element-count
      limit, structural safety checks — reject unsafe SVG before it ever
      reaches LittleFS (SPEC §10.2, DESIGN §9.2). Store the SVG file
      separate from the overlay JSON (`/config/floorplans/*.json` +
      `/config/floorplans/svg/`).
- [ ] Overlay document per SPEC §10.4 (viewBox, rooms, light positions,
      icons, glow radius/color) with `schema_version`.

### 8.2 Sanitization and coordinate core (webui)

- [ ] `sanitizeSvg.ts`: DOMParser-based (or DOMPurify-for-SVG if justified as
      a dependency) — strips `<script>`, `<foreignObject>`, `<iframe>`,
      `<object>`, `<embed>`, external `<use>`/`<image>`, external
      `@import`/`url()` in `<style>`, all `on*` attributes,
      `javascript:`/external URLs in `href`/`xlink:href`/`style`; enforces
      size and element-count limits. Runs on upload pre-check **and** before
      render.
- [ ] `SanitizedSvg` component: the **only** place `dangerouslySetInnerHTML`
      may appear (AGENTS.md §10).
- [ ] `coords.ts`: `screenToSvg`, `svgToScreen`, snap-to-grid — one shared
      implementation for both renderers; all stored positions in SVG
      `viewBox` space (DESIGN §9.1).

### 8.3 Control mode viewer (`FloorPlanViewer.tsx`)

- [ ] Inline sanitized SVG in one responsive `<svg>` with five layers in the
      same viewBox: original content, room overlay, lighting overlay, sensor
      overlay, interaction layer (DESIGN §9.3).
- [ ] `LightGlow`: radial-gradient circle per light, color from live state
      (or `glow_color`), opacity/radius mapped from brightness; **one shared
      `feGaussianBlur` filter**, never per light.
- [ ] Motion animations: power on/off 250 ms ease-out, brightness →
      opacity 0.15–1 + radius 60–100 %, color interpolation, hover/press
      spring, room selection outline. Animate only `opacity`, `transform`
      and gradient stops; honor `prefers-reduced-motion`.
- [ ] Interaction: tap light → toggle; tap again / long-press → control
      popover (mobile bottom sheet); tap room → select + room controls;
      zoom/pan (pinch, wheel, drag, +/−/reset, clamped 0.5×–6×, keyboard
      accessible); fullscreen with `Esc` hint.
- [ ] State source: `liveStore` only — **never simulate lighting state**
      (AGENTS.md §10). Equivalent accessible list view of the same lights
      (`role="switch"`, `aria-checked`, focusable).

### 8.4 Edit mode editor (`FloorPlanEditor.tsx`, React Konva)

- [ ] Lazy route; control mode must not import Konva (bundle test).
- [ ] Stage/Layers: sanitized SVG rasterized as a non-listening
      `Konva.Image` (Blob URL → `<img>`; scripts never execute in image
      context); objects layer with draggable light nodes.
- [ ] Features (DESIGN §9.4): drag, select, multi-select, `Transformer`
      glow-radius handle, arrow-key nudge, snap-to-grid toggle, align/
      distribute, delete, undo/redo (history in `floorplanEditorStore`),
      zoom/pan. Side panel: searchable assignment list of unplaced lights,
      icon, glow radius/color. Unassigned-lights tray (drag onto plan).
- [ ] Room regions drawn as polygons/rectangles stored as overlay data.
- [ ] Autosave draft in the store; **Save** → `PUT /floorplans/{id}/layout`;
      **Cancel** discards; warn on unsaved exit.

### 8.5 Mode switching and tests

- [ ] Segmented **Control | Edit** toggle in the toolbar; edit mode shows a
      persistent "Editing" badge + tinted border; deep links default to
      control; unsaved-change prompt on switch (DESIGN §9.5).
- [ ] **Mandatory sanitizer test suite** with malicious samples: `<script>`,
      `onload`, `javascript:` href, external `<image>`, `<foreignObject>`,
      `@import`, oversized file, element-count bomb — assert stripping
      (DESIGN §16).
- [ ] Unit tests: `coords.ts` round-trips, glow mapping, mode-switch guard.
- [ ] Playwright: upload → place lights → save → control mode glow follows
      mocked live state; axe passes in both modes; mobile viewport sheet.

## Contracts & interfaces introduced

- Floor-plan document + layout PUT payload (Zod ↔ firmware schema).
- SVG upload constraints (size/elements) documented in `docs/API.md`.

## Gate (acceptance criteria)

1. Floor plans upload, save and survive reboot; overlay data never touches
   the SVG file.
2. Lights positioned in edit mode appear at identical coordinates in control
   mode (same viewBox space — proven by a test).
3. Visual state follows actual hardware state in control mode; no local
   simulated state exists in code review.
4. Malicious-SVG suite passes on both client and firmware validators.
5. Konva loaded only on the editor route; viewer chunk ≤40 KB, editor chunk
   ≤90 KB gzipped (DESIGN §14.1).
6. Edit and operate modes are visually unmistakable and cannot be confused.

## Docs to update

`docs/API.md` (floorplan endpoints + SVG limits), `docs/UI_UX.md`,
`docs/ARCHITECTURE.md`, `docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- Large SVGs on phones: measure render cost early; consider flattening deep
  groups at sanitize time if profiling shows jank.
- Konva + DOMPurify weight vs budget: justify each dependency (AGENTS.md §14)
  before adding.
