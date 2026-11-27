# AGENTS.md

Rules for AI coding agents working on Lumen, the universal ESP32 smart lighting controller.

Read this file first. Then read `SPEC.md` (product and firmware spec), `DESIGN.md` (WebUI design system) and `SKILL.md` (step-by-step development procedure) for the area you are touching.

Implementation follows the phase plan in `.opencode/plans/` — start with `.opencode/plans/README.md` (phase index, dependency graph, conventions), then open the file for the current phase. Before working on a phase, load the skill mapped to it in `.opencode/skills/` (start with `lumen-overview`). A phase is finished only when its gate in the plan passes.

## 1. Project

Lumen is a universal smart lighting controller for ESP32 boards, with a React web UI served from the device.

| Area | Stack |
|---|---|
| Firmware | ESP-IDF (pinned), PlatformIO, C++17, FreeRTOS |
| Storage | NVS (secrets, critical settings), LittleFS (JSON config, web assets) |
| Web UI | React, TypeScript, Vite, shadcn/ui, Tailwind CSS, Motion for React, GSAP (optional), React Konva, React Flow, Lucide React, Zustand, TanStack Query, React Hook Form, Zod, Recharts, Sonner |

The KinCony KC868-AP is the first reference board. The application must stay hardware-independent.

## 2. Repository layout

```text
src/ or components/   firmware modules (hal, drivers, lighting, automation, net, web, storage)
boards/               built-in board profiles (JSON)
schemas/              JSON schemas for profiles, lights, scenes, automations, floor plans
webui/                React app (see DESIGN.md section 3 for structure)
test/                 unit, integration, hardware, fixtures
docs/                 ARCHITECTURE, API, HARDWARE_PROFILES, AUTOMATION, ESP_NOW_PROTOCOL, UI_UX, IMPLEMENTATION_STATUS
.opencode/            plans/ (phase-by-phase implementation plan), skills/ (phase agent skills)
controller-presets/   vendor reference code and pinmap for the KC868-AP (read-only reference, never a source of verified facts)
protocol/             Lumen Pro wire contract: JSON Schemas + fixtures + ticket vectors (shared by every product)
cloud/                Lumen Cloud API (Python, FastAPI) + docker-compose dev stack
gateway/              Lumen Gateway (Go, Linux) — the only path from controllers to the cloud
mobile/               Lumen mobile app (React Native, Expo)
assets/               logo (SVG) and product images
```

If the actual layout differs, follow the repository and update this section.

## 3. Commands

Environment names: `esp32` (KC868-AP reference board + flash/monitor ports), `esp32s3` (compile target), `native` (host tests). Keep this table accurate.

| Task | Command |
|---|---|
| Build firmware | `pio run -e esp32` (or `pio run -e esp32s3`) |
| Flash reference board | `pio run -e esp32 -t upload` (port pinned to `/dev/ttyUSB0`) |
| Serial monitor | `pio device monitor -p /dev/ttyUSB0 -b 115200` |
| Host unit tests | `pio test -e native` |
| On-target test suites | `pio test -e esp32` (`test_device_*` only; runs once Phase 3+ suites exist) |
| Build LittleFS image | `pio run -e esp32 -t buildfs` |
| Install web UI deps | `cd webui && npm ci` |
| Web UI dev server | `cd webui && npm run dev` (proxies `/api` to `LUMEN_CONTROLLER_URL`, default `http://127.0.0.1:8080`) |
| Web UI lint and typecheck | `cd webui && npm run lint && npm run typecheck` |
| Web UI tests | `cd webui && npm test` |
| Web UI bundle budget | `cd webui && npm run check:budget` |
| Web UI build | `cd webui && npm run build` |
| Web UI e2e (real device) | `cd webui && LUMEN_URL=http://<controller-ip> LUMEN_PASSWORD=<admin password> npm run test:e2e` (creates/removes `e2e_*` lights on PCA9685 channels 14–15) |
| MQTT / Home Assistant / TCP / UDP integration suite | `cd test/integration/ha && ./setup.sh && python3 ha_test.py http://<controller-ip> '<admin password>'` (docker: Mosquitto :18830, HA :8124) |
| Firmware / UI update over the network | Settings → Firmware / Web interface, or `POST /api/v1/ota/upload` / `/system/ui/upload` with the raw file; `/recovery` if the UI is missing |
| OTA / backup drills on the device | `python3 test/device/maintenance_test.py <url> <password> <dir with ok.bin crash.bin unhealthy.bin>` (images: `LUMEN_VERSION=… LUMEN_OTA_SELFTEST=2 pio run -e esp32`) |
| Security suite (real device) | `python3 test/integration/security_test.py http://<controller-ip> '<password>' --secrets 'wifi,mqtt,…'` (rate-limit check last; locks this IP out for ≤ 5 min) |
| Reliability suite (real device, test image) | `LUMEN_TEST_HOOKS=1 LUMEN_VERSION=0.0.0-reliability pio run -e esp32`, install with `test/device/ota_install.py`, then `python3 test/integration/reliability_test.py <url> '<password>' [--peer <url B>]`; re-install the normal image afterwards |
| Release packaging | `cd webui && npm run build:fs`, `pio run -e esp32 && pio run -e esp32 -t buildfs`, `python3 scripts/package_release.py esp32 <version> dist/`. CI does this when a GitHub release is published (`.github/workflows/release.yml`; tag `v<version>` must match `version.txt`) |
| Performance / soak (real device) | `python3 test/integration/perf_test.py <url> '<password>'`; `python3 test/integration/soak_test.py <url> '<password>' --hours 24` (CSV + summary in `test/soak/results/`) |
| Two-controller ESP-NOW suite | `python3 test/integration/multi_test.py http://<A> http://<B> '<password>'`; e2e: add `LUMEN_PEER_URL=http://<B>`; flash a second board with `pio run -e esp32 -t upload --upload-port /dev/ttyUSB1`; `test/device/ota_install.py <password> <bin> <url…>` installs over the network |
| Device test helpers | `test/device/setup_demo.py <url> <password>` (demo rooms/lights), `test/device/lumen_udp.py` (signed UDP client) |
| README screenshots (real device) | `cd webui && LUMEN_URL=http://<controller-ip> LUMEN_PASSWORD=<pw> node scripts/readme_screenshots.mjs` (needs a floor plan `readme-demo` and automation `evening_lights`, or set `LUMEN_PLAN` / `LUMEN_AUTOMATION`; writes `docs/images/`) |
| Validate schemas | `npm run validate:schemas` (repo root; first `npm ci` at root installs ajv; also checks `protocol/`) |
| Protocol fixtures / ticket vectors | `python3 protocol/tools/make_fixtures.py` (regenerate), `python3 protocol/tools/ticket_ref.py` |
| Gateway tests | `cd gateway && go vet ./... && go test -race ./...` (Go 1.27.4 at `~/.local/opt/go/bin`) |
| Cloud dev stack / tests | `cd cloud && make up` (postgres, redis, emqx, api on 127.0.0.1), `make test`, `make lint`; second replica `docker compose --profile replicas up -d --wait`; simulated gateway `uv run python tools/simgateway.py` |

## 3a. Lumen Pro (branch `lumen-pro`)

Commercial, proprietary product (see `LICENSE`). Before touching `protocol/`, `cloud/`, `gateway/` or `mobile/`,
or the firmware's cloud/ticket code, read `docs/pro/DECISIONS.md`, `docs/pro/PROTOCOL.md`,
`docs/pro/ARCHITECTURE.md` and the phase plan `.opencode/plans/pro/` (progress: `.opencode/plans/pro/STATUS.md`).

- Controllers never connect to Lumen Cloud. The Lumen Gateway is the only cloud path (DECISIONS D-17).
- A protocol change updates `docs/pro/PROTOCOL.md`, `protocol/schemas`, `protocol/tools/make_fixtures.py` (then
  regenerate) and every consumer in the same commit.
- The controller admin password never leaves the controller; remote and app access use local tickets.
- Dependencies must allow closed-source commercial use; record licences in `THIRD_PARTY_NOTICES.md`.
- Never push this branch to a public remote.

## 4. Architecture rules (mandatory)

1. Never hardcode board-specific GPIO numbers in application modules. Pins live in board profiles only.
2. All hardware access goes through HAL interfaces.
3. Board profiles are versioned JSON and validated by the `PinValidator` before apply.
4. Never assign one pin to two active peripherals.
5. Lighting control is independent of networking.
6. Automations run locally and need no internet or Home Assistant.
7. HTTP, MQTT, WebSocket and ESP-NOW callbacks only enqueue commands. They never run slow hardware work.
8. Subsystems talk through the internal event bus.
9. Persistence is transactional. A failed write or profile change must not destroy the last working configuration.
10. User configuration survives firmware upgrades. Migrations are never destructive.
11. The frontend and MQTT layer never touch GPIO. They send commands to the lighting engine.

## 5. Firmware rules

- ESP-IDF APIs only. No Arduino framework.
- C++17, RAII for resources.
- Check every `esp_err_t`. Never silently ignore errors.
- Bounded queues and buffers. No unbounded growth.
- Avoid dynamic allocation in hot paths.
- No uncontrolled task creation. Document each task's priority and stack size in `docs/ARCHITECTURE.md`.
- No blocking delays in lighting, transition or effect code.
- Handle hardware init failure without crashing the lighting engine.
- Never log passwords, tokens or secrets.
- Pin ESP-IDF and PlatformIO platform versions. Do not upgrade them unasked.

## 6. Hardware rules

- Check the board profile, MCU capability, reserved pins, strapping pins, input-only pins, peripheral conflicts and safe startup state before using a GPIO.
- MCU capability does not mean the board exposes it.
- Drivers are independent of application logic and testable with mocks.
- Do not let lighting channels write to I2C directly. Go through the PCA9685 driver and its batching service.
- The KC868-AP PCA9685 address `0x00` is unverified. Do not silently change it to `0x40`. Scan, report, and require confirmation.
- Do not claim polarity, addresses, Ethernet clock or RS485 direction are correct without physical verification. Mark such items "unverified on hardware".

## 7. Lighting and automation rules

- Use logical channels and the normalized `LightState`.
- Keep requested, applied and actual hardware state separate.
- Use the color engine for all conversions. Use non-blocking transitions. A newer command replaces an active transition or effect.
- Skip redundant hardware writes.
- Automations use trigger, condition, action. Validate before enabling.
- Prevent recursion and loops. Bound queues, execution time and rate. Support cancellation.
- Never execute user-supplied JavaScript or shell commands.

## 8. Networking and security rules

- Validate all received data. Version every protocol. Bound every buffer.
- Authenticate control operations (HTTP, WebSocket, TCP, UDP, ESP-NOW application layer).
- Implement reconnection. Lighting and local automation must keep working offline.
- Default credentials `admin` / `admin` require a forced password change on first login. Never ship them as a permanent production credential.
- Never create an unauthenticated permanent management hotspot.
- Hash passwords with salted PBKDF2-HMAC-SHA256. Store secrets in NVS, never in ordinary JSON, never in exports or logs.

## 9. Web UI rules

Follow `DESIGN.md`. In summary:

- React, TypeScript (strict), Vite, shadcn/ui, Tailwind CSS. Functional components only.
- TanStack Query owns REST data. Zustand owns live WebSocket state and UI/editor state. No duplicated copies of the same data.
- Zod schemas are the source of truth for API types. Forms use React Hook Form with the Zod resolver.
- Parse every API and WebSocket payload with Zod. Unwrap the `{success, data, error}` envelope in one place.
- Use design tokens. No hardcoded colors, except user-defined glow colors.
- Use Sonner for toasts and banners for persistent conditions.
- Lazy-load React Konva, React Flow, Recharts and GSAP. Respect the bundle budgets in `DESIGN.md` section 14.
- No CDN, no external fonts, no external requests. The UI works offline.
- Respect `prefers-reduced-motion`. Animate only `transform` and `opacity`.
- Never render a control for a property a light does not support.
- Do not store tokens in `localStorage`.
- Hardware-specific logic is not allowed in the frontend.

## 10. Floor-plan rules

- Sanitize SVG on upload and before render (and validate in firmware). `dangerouslySetInnerHTML` is allowed only inside the single `SanitizedSvg` component.
- Never modify the uploaded SVG to store lighting data. Keep overlay configuration separate.
- Store positions in SVG `viewBox` coordinates, shared by both renderers.
- Control mode uses inline SVG with Motion. Edit mode uses React Konva. Keep them separate and visually distinct.
- Visualize actual device state from the live store. Never simulate lighting state in the browser.

## 11. Testing

Every feature ships with tests.

- Firmware logic is tested on the host with mock drivers.
- A mock test does not prove physical hardware works. Mark real-hardware tests separately and report their status honestly.
- Web UI: Vitest, React Testing Library, MSW, Playwright, axe.
- SVG sanitizer, automation graph validation, command throttling and pin validation have dedicated test suites, including malicious or invalid input.

Before finishing a phase:

1. Firmware compiles.
2. Unit tests pass.
3. Web UI typechecks, lints and builds.
4. Web UI tests pass.
5. Schemas validate.
6. Memory and bundle size reviewed.
7. Documentation updated.

## 12. Documentation

Update the relevant docs whenever you change an API contract, JSON schema, board profile, protocol, config migration or UI workflow. Keep `docs/IMPLEMENTATION_STATUS.md` current.

## 13. Working method

For each task:

1. Inspect the existing code and docs.
2. Identify affected modules.
3. Plan briefly.
4. Implement the smallest complete feature.
5. Add tests.
6. Build and validate.
7. Update docs.
8. Report what was done and what remains.

Work phase by phase using `.opencode/plans/` (the per-phase file lists tasks, tests and the exit gate; `docs/IMPLEMENTATION_STATUS.md` records progress). Do not implement everything in one pass. Keep the repository compiling between milestones. Do not mark something complete because code exists.

## 14. Prohibited shortcuts

- Hardcoded KC868-AP GPIOs outside its board profile.
- Fake hardware state or placeholder APIs marked as implemented.
- Empty files presented as finished modules.
- Arbitrary pin allocation.
- Blocking delays in lighting control.
- Insecure production defaults.
- Destructive config migration.
- Silent error suppression.
- Unnecessary dependencies. Justify every new package, especially on the web UI side where bytes live in flash.
- Upgrading pinned dependency versions without being asked.

## 15. Ask before

- Changing the partition table, flash layout or pinned toolchain versions.
- Changing a persisted schema in a way that needs migration.
- Adding a dependency.
- Anything that factory-resets, erases flash or changes security defaults.

## 16. Definition of done

A feature is done only when implementation, validation, tests, a passing build and updated docs all exist, and known limitations are written down. The final report states: what was implemented, modules changed, tests run, build results, hardware verification status, and remaining issues.
