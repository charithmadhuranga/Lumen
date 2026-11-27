# Lumen Pro — Status

Progress record for [README.md](README.md). Verification labels: **mock**,
**local docker**, **host Linux**, **device** (real controller), **Pi**,
**phone**.

| Phase | State | Notes |
|---|---|---|
| P0 Foundation | done (gate passed 2026-11-27) | see log |
| P1 Cloud foundation | done (gate passed 2026-11-27) | local docker |
| P2 Accounts and homes | done (gate passed 2026-11-27) | local docker |
| P3 Devices and identity | not started | |
| P4 Cloud messaging | not started | |
| P5 Firmware (tickets, gateway pairing) | not started | no cloud connection on controllers (D-17) |
| P6 Gateway | not started | |
| P7 Mobile app | not started | |
| P8 Config sync | not started | |
| P9 OTA | not started | |
| P10 Notifications and audit | not started | |
| P11 Production and release | not started | |

## Open owner decisions

| # | Decision | Needed by |
|---|---|---|
| O-0 | Gateway extra features: confirm the D-18 first-release list (fleet management, backups, OTA relay, home-wide scenes/automations) and pick any candidates | P6 |
| O-1 | Accept D-07 (controller stays the configuration authority; the cloud mirrors it) instead of the brief's cloud authority | P8 (planned on D-07) |
| O-2 | Broker licence for production: EMQX version and terms, or Mosquitto (D-12) | P11 |
| O-3 | Cloud provider and region; domain name | P11 |
| O-4 | Email provider | P10 |
| O-5 | Secure boot / flash encryption on shipped units | P11 |
| O-6 | Company / legal entity name for the copyright notice | before first sale |

## Log

| Date | Phase | Item | Result |
|---|---|---|---|
| 2026-11-27 | P0 | Branch `lumen-pro`, proprietary LICENSE, `docs/pro/` (VISION, DECISIONS, PROTOCOL, ARCHITECTURE), this plan | done |
| 2026-11-27 | P0 | Owner decision: controllers reach the cloud only through the gateway (D-17); D-05/D-06 superseded; plan, protocol, schemas updated | done |
| 2026-11-27 | P0 | `protocol/`: 10 schemas, 29 valid + 56 invalid fixtures, 17 ticket vectors; `npm run validate:schemas` | PASS (mock) |
| 2026-11-27 | P0 | Ticket vectors: Python reference and Go (`gateway/internal/ticket`, `go test -race`), Go mints identical bytes | PASS (host Linux) |
| 2026-11-27 | P0 | Gateway cross-build linux/arm64, linux/arm/7, linux/amd64 (Go 1.27.4) | PASS (host Linux) |
| 2026-11-27 | P0 | THIRD_PARTY_NOTICES.md generated: 6 firmware, 127 web UI deps, all on the commercial-use allowlist | PASS |
| 2026-11-27 | P0 | README / AGENTS.md updated for Lumen Pro; `.github/workflows/pro.yml`; release workflow refuses public repos | done (CI not yet run) |
| 2026-11-27 | P0 | Firmware regression build `pio run -e esp32` (no firmware changes in P0) | PASS |
| 2026-11-27 | P1 | Cloud: ruff + mypy --strict clean; 35 pytest tests incl. live broker refusal | PASS (local docker) |
| 2026-11-27 | P1 | `make up`: postgres 16.16, redis 7.4.12, emqx 5.8.9 (TLS only), api image (python 3.12.16-slim) all healthy; `/readyz` 200 | PASS (local docker) |
| 2026-11-27 | P1 | 41 cloud production deps added to THIRD_PARTY_NOTICES.md (certifi MPL-2.0, unmodified, server side) | done |
| 2026-11-27 | P2 | Accounts (argon2id, JWT + rotating refresh with reuse detection, sessions, lockout, rate limits, verify/reset), homes, members, rooms, role matrix (60 route×role cases incl. outsider 404) | PASS — 151 tests (local docker) |
| 2026-11-27 | P2 | HTTP smoke test against the rebuilt API container | PASS (local docker) |
