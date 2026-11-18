# Test fixtures

Shared JSON documents used by schema validation and (later) firmware tests.

Naming convention enforced by `scripts/validate_schemas.mjs`:

- `<schema>.valid.json` — must validate against `schemas/<schema>.schema.json`
- `<schema>.invalid.json` — must FAIL validation (proves the schema rejects)

Current fixtures: board-profile, light, room, group, scene, automation,
floorplan (each has a `.valid.json`; board-profile and automation also have
`.invalid.json`).

Later phases add: board profile fixtures (Phase 2/3), malicious SVG samples
(Phase 8), automation graph fixtures (Phase 9).
