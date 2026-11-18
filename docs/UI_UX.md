# UI / UX

> **Not yet implemented** (Phase 0 stub — Phase 6 fills this file).
>
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

## Known gaps

- No routes/screens yet beyond the Phase 0 landing (Phases 6+).
- Bundle budgets measured, not yet enforced per-chunk (Phase 6 adds
  manualChunks; CI fails on budget from Phase 6).
