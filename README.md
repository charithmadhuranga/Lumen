# Lumen

Universal ESP32 smart lighting controller — firmware + web UI served from
the device. Hardware-independent application core with the KinCony
KC868-AP as the reference board.

- **Firmware:** ESP-IDF (pinned 6.1.0 via PlatformIO `espressif32@8.0.0`),
  C++17, FreeRTOS.
- **Web UI:** React + TypeScript (strict) + Vite + Tailwind v4 + shadcn/ui,
  served from LittleFS, works offline.
- **Runs locally:** lighting, rooms, groups, scenes, automations — no
  internet or cloud required.

## Repository layout

```text
src/                 firmware modules (core, hardware, lighting, automation,
                     networking, web, services) — SPEC §22 tree
include/             shared firmware headers
boards/              board profiles (JSON, validated by schemas/)
schemas/             JSON Schemas (draft 2020-12) for every persisted document
webui/               React app (see DESIGN.md for structure)
test/                host unit tests (test_host_*) + JSON fixtures
docs/                architecture, API, hardware, status tracking
.opencode/plans/     phase-by-phase implementation plan (start with README.md)
.opencode/skills/    per-phase agent skills (start with lumen-overview)
controller-presets/  vendor reference only — never a source of verified facts
```

## Commands

| Task | Command |
|---|---|
| Build firmware | `pio run -e esp32` (or `-e esp32s3`) |
| Flash reference board | `pio run -e esp32 -t upload` (port pinned: `/dev/ttyUSB0`) |
| Serial monitor | `pio device monitor -p /dev/ttyUSB0 -b 115200` |
| Host unit tests | `pio test -e native` |
| LittleFS image | `pio run -e esp32 -t buildfs` |
| Install web UI deps | `cd webui && npm ci` |
| Web UI dev server | `cd webui && npm run dev` (proxies `/api` → `LUMEN_CONTROLLER_URL`) |
| Lint + typecheck | `cd webui && npm run lint && npm run typecheck` |
| Web UI tests | `cd webui && npm test` |
| Web UI build | `cd webui && npm run build` |
| Validate schemas | `npm run validate:schemas` (repo root) |

## Getting started

1. Read `AGENTS.md` (rules), then `.opencode/plans/README.md` (phase index).
2. Progress lives in `docs/IMPLEMENTATION_STATUS.md`.
3. Architecture: `docs/ARCHITECTURE.md`; hardware facts:
   `docs/HARDWARE_PROFILES.md` (verified vs unverified — clearly separated).

## Current status

Release candidate **1.0.0-rc.1**: all implementation phases (0–12) are complete
and tested on the KinCony KC868-AP. See [CHANGELOG.md](CHANGELOG.md) for the
feature list and known limitations, and `docs/IMPLEMENTATION_STATUS.md` for the
evidence. Open before 1.0.0: the 24-hour soak test, and the hardware checks for
Ethernet, RS485 and relay polarity.

## Installing

Download the files from the GitHub release and follow
[docs/FLASHING.md](docs/FLASHING.md): browser or `esptool` for a fresh install,
**Settings → Firmware** / **Web interface** to update a running controller.
Publishing a GitHub release for tag `v<version>` (matching `version.txt`)
builds and attaches the files automatically.
