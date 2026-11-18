# Phase 12 — Security, OTA and Production Hardening

**SPEC §26 Phase 12, SPEC §16, §19.4, §20, §21, §24, §25.6** ·
**Skill:** `lumen-security-ota`

## Outcome

Production-ready firmware: hardened authentication, secure storage, rollback
capable OTA, backup/restore, complete diagnostics, measured performance, and
a passing reliability/security test suite.

## Dependencies

All previous phases (this phase hardens and completes them).

## Tasks

### 12.1 Authentication hardening (SPEC §16)

- [ ] Audit and complete: rate limiting on login and sensitive endpoints,
      session timeout, cookie flags (HttpOnly, SameSite, Secure where
      applicable), CSRF on every mutating route, session invalidation on
      password change/logout, API request-size limits, WebSocket
      authentication re-check.
- [ ] Default credential lifecycle proven end-to-end: `admin`/`admin` forces
      change on first login and can never persist (test).
- [ ] Security review of every endpoint added in Phases 6–11: auth required,
      input validated, no secret leakage in errors/logs/responses.

### 12.2 Secure storage (SPEC §16.5)

- [ ] Secrets exclusively in NVS; audit confirms none in JSON config,
      backups, exports, logs or diagnostics (automated grep-style test over
      fixtures + backup archives).
- [ ] Evaluate NVS encryption and flash encryption for production; document
      enablement steps and their implications — enabling flash encryption
      needs user sign-off (AGENTS.md §15).

### 12.3 OTA and recovery (SPEC §20)

- [ ] `ota_manager.*`: dual OTA app partitions, version + metadata readout,
      upload progress, image validation (magic, size, partition fit,
      version/compatibility checks), **never activate a failed image**,
      automatic rollback on boot failure (anti-rollback via app descriptors),
      configuration preservation across updates.
- [ ] Endpoints: `GET /ota/status`, `POST /ota/upload`; UI: Settings → OTA
      upload with progress + validation result (DESIGN §7.11).
- [ ] Update must not depend on the WebUI staying connected (chunked upload
      + state machine resumable by the device itself).
- [ ] Document signed-image verification as the production path; enable
      signing if the user requests it.

### 12.4 Backup, restore and factory recovery

- [ ] `GET /backup`, `POST /backup/restore` (SPEC §17.2): full + partial
      backup, version compatibility check, import validation, restore
      confirmation, transactional restore (failed restore leaves current
      config intact). Secrets excluded from portable exports by default.
- [ ] UI: Settings → Backup & restore; restart and factory reset behind
      typed-confirmation dialogs (DESIGN §7.11).
- [ ] Corrupted-config recovery drill: corrupt each document type, confirm
      backup → defaults fallback with no crash (reuses Phase 1 tests,
      extended to real flash).

### 12.5 Diagnostics and logging completion

- [ ] Diagnostics page fields per SPEC §21: firmware version, MCU info,
      free/min heap, task stats, network state, I2C devices, hardware
      health, active channels, active automations, peers, recent errors.
- [ ] Log-category audit: every subsystem logs under its category; redaction
      guard tested (attempted secret logging never appears).
- [ ] Watchdogs wired for all long-running tasks; task registry in
      `docs/ARCHITECTURE.md` complete and accurate.

### 12.6 Performance and reliability (SPEC §24–25)

- [ ] Measure and record against targets: local command <100 ms, UI
      propagation <250 ms, ≥30 updates/s transitions, automation trigger
      <100 ms.
- [ ] Memory audit: no per-update allocations in hot paths, bounded queues
      everywhere (queue-overflow counters tested), heap fragmentation
      monitored, no frequently-changing state written to flash.
- [ ] Reliability suite (SPEC §25.6): MQTT/Wi-Fi disconnect, ESP-NOW peer
      loss, I2C peripheral failure, invalid/corrupted config, queue overflow,
      low memory, repeated light commands, long transitions, OTA interruption
      — each with a recorded result.
- [ ] Long-duration soak: ≥24 h continuous operation with lights cycling,
      automation running, heap watermark stable (record result).

### 12.7 Documentation close-out

- [ ] Every doc current: API, architecture (tasks/queues/priorities),
      hardware profiles (verification status), automation, ESP-NOW protocol,
      UI/UX, implementation status.
- [ ] Known-limitations list maintained honestly (unverified hardware items,
      heap trade-offs, unsupported cases).

## Contracts & interfaces introduced

- OTA status/state machine consumed by the Settings UI.
- Backup manifest format (versioned, secrets-free).

## Gate (acceptance criteria)

1. Security tests pass (auth lifecycle, rate limits, CSRF, secret-leak
   scans, WS auth).
2. OTA: valid update applies and survives; interrupted/corrupt update rolls
   back to the previous app with config intact.
3. Configuration survives a firmware update (upgrade-in-place drill).
4. Reliability suite + 24 h soak pass with recorded results.
5. Performance targets measured and met or explicitly documented as
   deviations.
6. Diagnostics page complete; no secrets in any log/export/response.

## Docs to update

All `docs/*` (final pass), `docs/IMPLEMENTATION_STATUS.md`, known-limitations
section (README or ARCHITECTURE), then proceed to `release-gate.md`.

## Risks / open questions

- Flash encryption / secure boot change the OTA and debugging workflow —
  ask before enabling.
- Signed OTA images need a key-management decision from the user.
