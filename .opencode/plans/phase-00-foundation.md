# Phase 0 — Repository and Project Foundation

**SPEC §26 Phase 0** · **Skill:** `lumen-overview`

## Outcome

A monorepo that builds on day one: PlatformIO/ESP-IDF firmware that compiles,
a React/Vite web UI that builds, host unit tests that run, schema validation,
CI checks, and the documentation skeleton. No product features yet.

## Dependencies

None. This phase starts the project.

## Tasks

### 0.1 Firmware project scaffold

- [x] Create `platformio.ini` with pinned PlatformIO platform and ESP-IDF
      versions (no auto-upgrade, AGENTS.md §5).
      Environments: `esp32` (reference/dev), `esp32s3`, `native` (host tests).
- [x] Create `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`
      (dual OTA app partitions + littlefs data — **ask before finalizing the
      partition table**, AGENTS.md §15).
      Status: initial 4 MB layout created and validated by a host test;
      confirmation still open — see Risks.
- [x] Create the SPEC §22 source tree: `src/core/`, `src/hardware/`,
      `src/lighting/`, `src/automation/`, `src/networking/`, `src/web/`,
      `src/services/`, `include/`.
- [x] Add a minimal `src/main.cpp` that boots, installs the default log
      format and prints firmware version — nothing more. (Plus a 5 s
      heartbeat as the on-device Phase 0 proof of life.)
- [x] Add `.clang-format` and `.clang-tidy`.

### 0.2 Web UI scaffold

- [x] Create `webui/` per DESIGN §3: Vite + React + TypeScript (strict).
- [x] **Check the Tailwind major version (v3 vs v4) before following any setup
      snippet** (DESIGN §2 version policy), then shadcn/ui init.
      Checked: Tailwind **v4.3.5** → CSS-first setup, no `tailwind.config.ts`.
      shadcn init with `radix` base; preset output adjusted per DESIGN (see
      `docs/UI_UX.md` decisions log).
- [x] Install and pin (lockfile) only the Phase-0 set: shadcn base components,
      Tailwind, Sonner, Lucide, zod. Defer Konva / React Flow / Recharts /
      GSAP to their phases. (Also: motion — required by the MotionConfig
      task below; React Router deferred to Phase 6 routing.)
- [x] Add design tokens as CSS variables (DESIGN §4.1–4.2), light/dark/system
      theme toggle, `MotionConfig reducedMotion="user"`.
- [x] npm scripts: `dev` (with `/api` + `/api/v1/ws` proxy via env var),
      `build`, `lint`, `typecheck`, `test`, `test:e2e`.
- [x] Pin exact versions in `package-lock.json`.

### 0.3 Schemas and validation

- [x] Create `schemas/` with initial JSON Schema documents (draft 2020-12):
      `board-profile.schema.json` (content from SPEC §6.2), `light`,
      `room`, `group`, `scene`, `automation`, `floorplan`.
- [x] Add `npm run validate:schemas` (repo-root script) that validates every
      schema file against the meta-schema and validates fixture documents.
      Tool: ajv 2020-12 (user-approved dependency). Fixtures follow
      `test/fixtures/<schema>.valid.json` / `.invalid.json`; `boards/*.json`
      is validated too once profiles land.

### 0.4 Host tests and CI

- [x] Wire `pio test -e native` with a smoke test. Suites:
      `test/test_host_smoke/` (version header) and
      `test/test_host_partitions/` (partition-table contract) — 10 tests,
      all passing. Naming convention: `test_host_*` (host) / `test_device_*`
      (on-target); `test/fixtures/` is never collected as a suite.
- [x] Create `test/fixtures/` for shared JSON fixtures (profiles, scenes,
      malicious SVG samples come later in Phase 8).
- [x] CI pipeline: firmware build matrix (`esp32`, `esp32s3`), host tests,
      `validate:schemas`, web UI `lint` + `typecheck` + `test` + `build`.
      Bundle-size check is a **real initial-budget check**
      (`webui/scripts/check_bundle_budget.mjs`, DESIGN §14.1 initial JS/CSS);
      per-chunk budgets are added in Phase 6 with code splitting.
      Workflow created and **green on `main`** (run 44293596397, all 5
      jobs); action pins bumped off the deprecated Node 20 runtime.
- [x] `.gitignore` covering `.pio/`, `node_modules/`, `dist/`, generated
      LittleFS data.

### 0.5 Documentation and tracking

- [x] `docs/ARCHITECTURE.md` skeleton: layer diagram (SPEC §4.1), task
      registry table (owner, priority, stack — filled as tasks appear,
      AGENTS.md §5), event-bus overview.
- [x] `docs/IMPLEMENTATION_STATUS.md` with every phase checkbox from SPEC
      §29 Rule 6 (this is the canonical progress record), plus the
      hardware verification log (Rule 5).
- [x] `docs/API.md`, `docs/HARDWARE_PROFILES.md`, `docs/AUTOMATION.md`,
      `docs/ESP_NOW_PROTOCOL.md`, `docs/UI_UX.md` stubs with "Not yet
      implemented" headers so later phases fill them instead of creating them.
- [x] `README.md`: what the project is, layout, commands, link to plans.
- [x] `boards/` directory with a placeholder note (real profiles land in
      Phase 2).

## Contracts & interfaces introduced

- Build/test/lint command surface (recorded in `AGENTS.md` §3 — update it the
  moment real commands differ).
- Schema version field convention: every persistent document carries
  `schema_version` (SPEC §19.3).
- Storage key convention: theme state in `localStorage` key `lumen-theme`
  (owner moves to uiStore in Phase 6, key unchanged).
- Dev proxy env var: `LUMEN_CONTROLLER_URL` (default `http://127.0.0.1:8080`).

## Gate (acceptance criteria)

1. `pio run -e esp32` and `pio run -e esp32s3` succeed. ✅ (local)
2. `pio test -e native` runs and passes a smoke test. ✅ (10/10)
3. `cd webui && npm ci && npm run build` succeeds; `lint` and `typecheck`
   pass. ✅
4. `npm run validate:schemas` passes. ✅ (7 schemas, 9 fixtures)
5. CI green on the default branch. ✅ (private repo
   `charithmadhuranga/Lumen`, run 44293596397 — all 5 jobs green)
6. Project structure documented in `README.md` and `docs/ARCHITECTURE.md`;
   `AGENTS.md` §3 command table matches reality. ✅
7. `docs/IMPLEMENTATION_STATUS.md` exists and Phase 0 boxes are checked. ✅

On-device (extra, KC868-AP on `/dev/ttyUSB1`): firmware flashed, boots,
banner + partition table + 5 s heartbeat observed over serial — logged in
`docs/IMPLEMENTATION_STATUS.md`. ✅

## Docs to update

`README.md`, `docs/ARCHITECTURE.md`, `docs/IMPLEMENTATION_STATUS.md`,
`AGENTS.md` §3 (commands), `.opencode/plans/README.md` (if sequencing changes).

## Risks / open questions

- Partition table: **confirmed by user 2026-11-18** — keep the initial 4 MB
  layout (dual OTA 1375 KB ×2 + LittleFS 1125 KB). Any later change is a
  fresh AGENTS.md §15 ask.
- ESP-IDF/PlatformIO version pinning: pinned `espressif32@8.0.0`
  (ESP-IDF 6.1.0), PlatformIO 6.2.0 in CI — changing them later is an
  explicit, user-approved task.
- `test:e2e` fails by design until Playwright lands in Phase 6 (honest
  placeholder, not a fake-green check).
