---
name: lumen-floorplan
description: Floor-plan system work for Lumen — SVG sanitization, viewBox coordinate math, control mode (inline sanitized SVG + Motion glows driven by live state) and edit mode (React Konva placement), room regions, mode switching, and the malicious-SVG test suite. Use for anything under webui floorplan features, floor-plan REST endpoints, or SVG upload validation in firmware. Triggers: "floor plan", "svg sanitizer", "konva", "glow", "viewbox", "floorplan upload", "phase 8".
---

# ESP Lighting — Floor Plan

**Plan:** `.opencode/plans/phase-08-floorplan-editor.md`.
**Spec:** SPEC §10; DESIGN §9 (authoritative).

## Non-negotiables

1. **The uploaded SVG is never modified** to store lighting data. Overlay
   (rooms, light positions, glow settings) lives in separate JSON.
2. **Sanitize on upload (client pre-check + firmware validation) and before
   render.** `dangerouslySetInnerHTML` exists in exactly one place: the
   `SanitizedSvg` component (AGENTS.md §10).
3. **One coordinate space:** all stored positions are SVG `viewBox`
   coordinates via `coords.ts` (`screenToSvg`, `svgToScreen`, snap) — shared
   by both renderers so a position means the same thing in edit and control
   mode.
4. **Two renderers, never mixed:** control mode = inline sanitized SVG with
   Motion overlays (no canvas, no Konva import); edit mode = React Konva
   stage with the sanitized SVG rasterized as a non-listening `Konva.Image`.
5. **Actual device state only.** Glows read `liveStore` — no simulated
   lighting state in the browser (AGENTS.md §10).

## Sanitization (mandatory suite)

Strip: `<script>`, `<foreignObject>`, `<iframe>`, `<object>`, `<embed>`,
external `<use>`/`<image>` hrefs, `<style>` with external `@import`/`url()`,
all `on*` attributes, `javascript:`/external URLs in
`href`/`xlink:href`/`style`. Enforce size (512 KB) and element-count limits.
Firmware must reject unsafe SVG too — the same rules, implemented server-side.
Your test suite must include samples for every bullet above (DESIGN §16);
add a new sample whenever you extend the sanitizer.

## Control mode

- One `<svg>` with the same `viewBox`, five layers in order: original
  content → room overlay → lighting overlay → sensor overlay → interaction
  layer.
- `LightGlow`: radial-gradient circle per light; color from live state (or
  `glow_color`), opacity 0.15–1 and radius 60–100 % of `glow_radius` mapped
  from brightness; **one shared `feGaussianBlur` filter** — never one per
  light.
- Motion: power on/off 250 ms ease-out, color interpolation, hover/press
  spring, room selection. Animate only `opacity`, `transform`, gradient
  stops; honor `prefers-reduced-motion`.
- Interaction: tap light → toggle; again/long-press → popover (mobile bottom
  sheet); tap room → select + controls; zoom/pan (0.5×–6× clamp, keyboard
  accessible); fullscreen with `Esc` hint.
- Accessibility: focusable lights (`tabIndex=0`, `role="switch"`,
  `aria-checked`, labeled) + an equivalent list view for screen readers.

## Edit mode

- Konva is lazy-loaded — control mode must never import it (verify with the
  chunk split).
- Stage sized to container; viewBox maps via one scale + offset through
  `coords.ts`. Background: sanitized SVG as Blob-URL `<img>` (scripts don't
  run in image context).
- Features: drag/select/multi-select, `Transformer` glow handle, arrow-key
  nudge, snap toggle, align/distribute, delete, undo/redo
  (`floorplanEditorStore` history), zoom/pan; side panel assigns unplaced
  lights (searchable list) + icon + glow radius/color; room regions drawn as
  polygons/rectangles (overlay data).
- Draft autosaves to the store; **Save** → `PUT /floorplans/{id}/layout`,
  **Cancel** discards; unsaved-exit warning.

## Mode switching

Segmented **Control | Edit** toggle in the toolbar; edit mode shows a
persistent "Editing" badge + tinted border; deep links default to control;
switching with unsaved changes prompts Save / Discard / Cancel.

## Validation gate

- [ ] Upload → place → save → control: identical coordinates in both modes
      (automated test through `coords.ts`).
- [ ] Malicious-SVG suite green on client **and** firmware validators.
- [ ] Glows follow mocked live state; zero local simulated state in review.
- [ ] Viewer chunk ≤40 KB, editor chunk ≤90 KB gzipped; Konva absent from
      the viewer bundle.
- [ ] Playwright: full edit→control flow + mobile viewport; axe passes in
      both modes.

## Pitfalls

- Animating filter parameters or the full background (jank on phones).
- Storing pixel positions instead of viewBox positions.
- Re-sanitizing with different rules on client vs server (drift) — share the
  rule list, test both.
- Rendering per-light blur filters instead of one shared filter.
