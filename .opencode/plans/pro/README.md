# Lumen Pro — Implementation Plan

This plan sequences the work for the commercial Lumen Pro system: the
controller firmware, Lumen Cloud, the Lumen Gateway and the mobile app. The
documents to read first are:

- [docs/pro/VISION.md](../../../docs/pro/VISION.md): the brief.
- [docs/pro/DECISIONS.md](../../../docs/pro/DECISIONS.md): what changed in
  the brief, and why.
- [docs/pro/PROTOCOL.md](../../../docs/pro/PROTOCOL.md): the wire contract.
- [docs/pro/ARCHITECTURE.md](../../../docs/pro/ARCHITECTURE.md): the
  system architecture.

**Key rule (DECISIONS D-17):** controllers reach the cloud only through a
Lumen Gateway.

The firmware phases 0–12 (`../phase-*.md`) are finished (Lumen 0.1.0). Their
rules still apply to every firmware change (AGENTS.md).

## Rules for every phase

1. Work in order. A phase starts only after the gates of the phases it
   depends on have passed.
2. Every phase gate includes:
   - all existing firmware checks still pass: `pio test -e native`,
     `pio run -e esp32`, `pio run -e esp32s3`, plus the web UI lint,
     typecheck, tests and build;
   - the phase's own tests;
   - the `protocol/` fixtures pass in every consumer touched;
   - documentation and [STATUS.md](STATUS.md) updated.
3. Never mark a task done because code exists. Anything that talks to
   hardware, a phone or a real broker is recorded with how it was verified:
   "mock", "local docker", "device" or "phone".
4. The existing local behaviour of 0.1.0 never regresses. With
   `cloud.mode = off`, the firmware behaves exactly as before (tested).
5. New dependencies are pinned. The stack named in DECISIONS D-03, D-13 and
   D-14 is approved. Anything outside it needs a short justification in the
   phase's STATUS entry, and the owner is asked when it is not obviously
   required.
6. Never commit secrets. Development credentials live in `.env.example`
   (dummy values) and in files git ignores.

## Phases and dependencies

```text
P0  Monorepo, licence, protocol contract, CI skeleton
 |
P1  Cloud foundation (FastAPI, Postgres, Alembic, EMQX, compose, health)
 |
P2  Accounts and homes (users, auth tokens, homes, members, rooms)
 |
P3  Devices: gateways + controllers registry, claim, gateway credentials,
    broker auth/ACL hook, local tickets
 |
P4  Cloud messaging: commands, state, availability, events WebSocket
 |   (proven with a simulated gateway in tests)
 +--------------------+
 |                    |
P5  Firmware:        P6  Gateway (Go): cloud bridge, discovery,
    local tickets,       LAN link, adoption, fleet management,
    gateway pairing,     backups, setup, packaging
    Cloud card           (gate needs P5 on a real controller)
 |                    |
 +---------+----------+
           |
P7  Mobile app (auth, homes, onboarding, control, local/remote routing)
           |
P8  Configuration sync (mirror, config.apply, conflict handling, app editing)
           |
P9  OTA orchestration (signed manifests; controllers + gateway)
           |
P10 Notifications, event history, audit log, member invitations
           |
P11 Hardening, production deployment, factory provisioning, pilot, release gate
```

P6 can start its cloud side (against P4) and its LAN side (against a fake
controller) in parallel with P5. Its gate needs P5's tickets and pairing on a
real controller.

| Plan file | Phase | Main area |
|---|---|---|
| [p00-foundation.md](p00-foundation.md) | P0 | repo, protocol |
| [p01-cloud-foundation.md](p01-cloud-foundation.md) | P1 | cloud |
| [p02-accounts-homes.md](p02-accounts-homes.md) | P2 | cloud |
| [p03-devices-identity.md](p03-devices-identity.md) | P3 | cloud |
| [p04-cloud-messaging.md](p04-cloud-messaging.md) | P4 | cloud |
| [p05-firmware-cloud.md](p05-firmware-cloud.md) | P5 | firmware, local UI (tickets, pairing) |
| [p06-gateway.md](p06-gateway.md) | P6 | gateway (cloud bridge + hub) |
| [p07-mobile-app.md](p07-mobile-app.md) | P7 | mobile |
| [p08-config-sync.md](p08-config-sync.md) | P8 | all |
| [p09-ota.md](p09-ota.md) | P9 | all |
| [p10-notifications-audit.md](p10-notifications-audit.md) | P10 | cloud, mobile |
| [p11-production-release.md](p11-production-release.md) | P11 | all |

Progress is recorded in [STATUS.md](STATUS.md).

## Definition of done for Lumen Pro 1.0

The brief's §19 list applies, with one change: item 14 ("update firmware
through the managed cloud") applies to both controllers and gateways. It is
proven on:

- two KC868-AP controllers and one generic ESP32, all adopted by the
  gateway,
- one Raspberry Pi gateway,
- one Android phone, plus an iOS phone if available,
- a cloud deployed on real infrastructure.
