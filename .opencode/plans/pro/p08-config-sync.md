# P8 — Configuration sync (device-authoritative, DECISIONS D-07)

## Outcome

The app can rename lights, change rooms, and create, edit and delete
scenes and automations on any controller, remotely or locally. Concurrent
edits made in the local web UI are never lost.

## Tasks

- [ ] Firmware: REST writes accept `If-Match: <configRevision>` and
      return 412 when it is stale. That gives the gateway an atomic check
      for `config.apply` of the kinds `light` (name and room only, never
      the driver or pins), `room`, `group`, `scene` and `automation`.
  - Validation and the transactional `ConfigManager` path are the
    existing REST handlers, so nothing is duplicated.
  - The revision counter comes from P5.4.
- [ ] Gateway: maps `config.apply` onto the controller's REST CRUD
      endpoints with `If-Match: baseRevision`. A 412 becomes the result
      `conflict` with the current revision.
- [ ] Cloud:
  - `PATCH` and `POST` routes for lights, scenes and automations build
    `config.apply` commands;
  - responses report `pending`, then `applied`, or `conflict` with the
    current mirror;
  - the cloud-side room model maps to controller rooms. A cloud room spans
    controllers; on each controller the cloud keeps a room with the same
    name, created on demand.
- [ ] App: edit screens; the scene builder captures from current states;
      the automation editor is the simple form, mirroring the local UI's
      form editor. Conflicts are shown as "changed elsewhere, reload".

## Tests

- **Host:** revisions, conflicts, and the validation parity between REST
  and `config.apply`.
- **Cloud:** the command-building logic.
- **Gateway:** the mapping.
- **Device:** a race between a local UI edit and an app edit, where
  exactly one wins and the other gets a conflict.

## Gate

The tests pass, and the device race test passes on a real controller
behind the gateway.
