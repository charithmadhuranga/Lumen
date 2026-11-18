# ESP-NOW Protocol

> **Not yet implemented** (Phase 0 stub — Phase 10 fills this file).
>
> Planned for: SPEC §12 (multi-controller synchronization) — versioned
> binary envelopes, peer discovery, authenticated application-layer
> commands, conflict resolution across controllers, offline operation.
> Security rules: AGENTS.md §8 (validate everything, version every
> protocol, bound every buffer, authenticate control operations).

## Envelope format

(Phase 10: magic, version, type, flags, payload length, counter, payload,
integrity check)

## Message types

(Phase 10)

## Versioning and compatibility

(Phase 10: unknown versions rejected safely, never crash)

## Testing

(Phase 10/11: host tests with a mocked transport + two-device hardware test
recorded in the hardware verification log)
