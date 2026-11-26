# UI / UX

> The authoritative UI specification lives in `DESIGN.md` (tokens,
> components, layout, screens, states, testing, budgets). This file tracks
> UI implementation decisions and deviations as phases land.

## Decisions log

- 2026-11-18 (Phase 0): Tailwind **v4.3** confirmed as current major before
  scaffolding (DESIGN §2 policy); CSS-first setup, no `tailwind.config.ts`.
- 2026-11-18 (Phase 0): shadcn init with `radix` base, `nova` preset; preset
  output edited per DESIGN: Geist font import removed (§4.3 system stack),
  neutral token values replaced by DESIGN §4.1 palette, `next-themes`
  replaced by Lumen's own `useTheme` (§5.4 permits noted edits).
- 2026-11-18 (Phase 0): theme state lives in React context + `localStorage`
  key `lumen-theme`; Phase 6 migrates ownership to Zustand `uiStore`
  (DESIGN §2.1) keeping the same storage key.

- 2026-11-26 (Phase 8): floor plans — Control mode is the default for deep links; Edit mode shows an "Editing" badge,
  a tinted bordered frame and a dashed canvas border so the two cannot be confused. Lights are toggled by tapping the
  plan; full controls open via long-press / right-click / "i" / the "Controls" button in the accessible list (no
  double-tap: it would toggle twice). Glow radius is a slider in the side panel (no Konva Transformer handle).
- 2026-11-26: Konva colours come from the design tokens at runtime (canvas cannot read CSS variables); only user glow
  colours are literal. Konva = `react-konva/lib/ReactKonvaCore` + 5 shapes to stay inside the 90 KB editor budget.
- 2026-11-26: total UI image budget 500 KB gz (project check; DESIGN allows 1 MB) after LittleFS grew to 896 KB.
- 2026-11-25 (Phase 12): Settings → Firmware / Web interface / Backup & restore; uploads via XHR for progress, sharing
  the single envelope `unwrap()` with `request()`.

- 2026-11-26 (Phase 11): Devices → Controllers shows peers (online, last seen, signal) and their lights as switches;
  switching sends a command to the owner and the switch follows the owner's reported state. Integrations →
  Multi-controller holds enable / network id / key (show, paste, regenerate).

## Known gaps

- Initial JS budget is 160 KB in the check (DESIGN target 150 KB); currently 159.6 KB — any new initial-route code must be offset.
- No MSW mock-API mode; unit tests stub `fetch`/XHR.
