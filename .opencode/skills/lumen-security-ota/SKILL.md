---
name: lumen-security-ota
description: Security and production-hardening work for Lumen — authentication hardening, sessions/CSRF/rate limits, secret storage (NVS vs JSON), dual-partition OTA with validation and rollback, backup/restore, factory recovery, diagnostics completion, performance and reliability audits. Use for anything under src/web/auth, ota_manager, secure storage, Settings/OTA/backup UI, or the Phase 12 hardening pass. Triggers: "auth", "login", "session", "csrf", "password", "ota", "firmware update", "rollback", "backup", "restore", "security audit", "phase 12".
---

# ESP Lighting — Security, OTA & Production

**Plan:** `.opencode/plans/phase-12-security-production.md`.
**Spec:** SPEC §16 (security), §19.4 (backup), §20 (OTA), §21
(diagnostics), §24–25 (performance/reliability).

## Security baseline (must hold everywhere)

- **Credentials:** default `admin`/`admin` exists only to force a password
  change on first login — it can never persist (test this end-to-end).
  PBKDF2-HMAC-SHA256 with per-user salt; cryptographically secure random
  session tokens.
- **Sessions:** HTTP-only, SameSite cookies; configurable timeout; logout;
  invalidation after password change; login rate limiting; CSRF token on
  every mutating request.
- **Secrets:** NVS only — never in JSON config, backups, exports, logs,
  diagnostics or API responses. Integration secrets are write-only fields.
  Add an automated scan test (fixtures + generated backup) that fails if a
  known secret appears anywhere it shouldn't.
- **Every endpoint** added in Phases 6–11 gets audited here: authenticated,
  input-validated, size-bounded, no secret leakage in errors. WebSocket
  auth re-checked. ESP-NOW/TCP/UDP control authenticated at the application
  layer.
- Evaluating NVS encryption / flash encryption / secure boot / signed images:
  document the steps, but **ask before enabling** anything that changes the
  flash or debugging workflow (AGENTS.md §15).

## OTA (SPEC §20)

- Dual OTA app partitions; version + metadata exposed via `GET /ota/status`;
  `POST /ota/upload` with progress.
- Validate before activation: image magic, size, partition fit,
  version/compatibility — **never activate a failed image**; automatic
  rollback on boot failure (app-descriptor anti-rollback).
- Configuration survives updates (upgrade-in-place drill).
- Update state machine is device-owned — a dropped WebUI connection must
  not break the update.
- UI: Settings → OTA (progress + validation result), behind the confirm
  dialog pattern.

## Backup / restore / recovery

- `GET /backup`, `POST /backup/restore`: full + partial, version
  compatibility check, import validation, restore confirmation, transactional
  (failed restore leaves current config intact), secrets excluded from
  portable exports by default.
- Factory reset / restart only behind typed-confirmation dialogs.
- Recovery drill: corrupt each document type on real flash → backup →
  defaults fallback, no crash.

## Diagnostics & reliability close-out

- Diagnostics page complete per SPEC §21 (version, MCU, heap, tasks,
  network, I2C, health, channels, automations, peers, recent errors).
- Redaction audit: attempted secret logging never appears; every subsystem
  logs under its category.
- Measure vs SPEC §24 targets: command <100 ms, UI propagation <250 ms,
  transitions ≥30 updates/s, automation <100 ms.
- Memory audit: no per-update allocations in hot paths, all queues bounded
  with overflow counters, no frequent state writes to flash, fragmentation
  monitored.
- Reliability suite (SPEC §25.6): broker/Wi-Fi loss, peer loss, I2C failure,
  invalid/corrupted config, queue overflow, low memory, command floods, long
  transitions, OTA interruption — each recorded. Plus a ≥24 h soak with
  stable heap watermark.

## Validation gate

- [ ] Security tests green (auth lifecycle, rate limits, CSRF, WS auth,
      secret scans).
- [ ] OTA: good update applies; interrupted/corrupt update rolls back with
      config intact.
- [ ] Config survives a firmware upgrade.
- [ ] Reliability suite + 24 h soak recorded.
- [ ] Performance targets met or documented as deviations.
- [ ] Known-limitations list written; all docs current.

## Pitfalls

- "It works on HTTP in dev" — cookie flags and CSRF must be correct from
  Phase 6; this phase audits, it doesn't discover them for the first time.
- Shipping a factory-reset or flash-erase path without a typed confirmation
  and user approval.
- Marking OTA done because the upload endpoint returns 200 — the rollback
  drill is the test.
