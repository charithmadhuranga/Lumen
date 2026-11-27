# P9 — OTA orchestration

## Outcome

Owners update controllers and gateways from the app. Only compatible,
signed images are installed. Progress and results, including rollbacks,
are visible.

## Tasks

- [ ] Release signing:
  - An offline ECDSA P-256 release key (owner-held, never in CI secrets
    for production; the CI uses a separate staging key).
  - The manifest is `{product, version, hwModels/boards, channel, size,
    sha256, url, minVersion}`, signed over its canonical JSON form.
  - The public key is compiled into the firmware and the gateway.
- [ ] Cloud:
  - `ota_releases` (stable and beta channels) and `ota_jobs`;
  - upload is admin-only, via a CLI (`cloud/tools/release.py`) that
    pushes the image to S3 (MinIO in compose) with the signed manifest;
  - `POST /ota/jobs` checks compatibility against the reported
    board/model and version;
  - a `device.ota` command (op `ota.start` with the manifest) is sent to
    the device;
  - progress arrives through `events`.
- [ ] Gateway relay: on `ota.start`, the gateway:
  - downloads the image over HTTPS;
  - verifies the manifest signature, SHA-256, board and version;
  - uploads it to the controller through the existing
    `POST /ota/upload` (admin ticket);
  - watches the controller come back on the new version;
  - reports progress, success or rollback.

  Staged rollout within a home (D-18): one controller, then the rest.
- [ ] Firmware (defence in depth): `/ota/upload` also accepts the
      detached signature (header `X-Lumen-Signature`). When the controller
      is paired, an unsigned image from a ticket client is refused. Local
      admin sessions keep the existing unsigned upload, so owners can
      still install their own builds.
- [ ] Gateway self-update: download → verify the signature and SHA-256 →
      write `lumen-gateway.new` → atomic rename → systemd restart. On a
      failed health check within 60 s, the previous binary is restored.
- [ ] App: an update badge, release notes, start the update, progress.

## Tests

- **Signature:** a bad signature, wrong board, downgrade below
  `minVersion` and a truncated download are all refused (host and Go
  tests).
- **Device:** an update through the gateway; a crash image rolling back
  and being reported (reusing the `test/device/maintenance_test.py`
  images); a staged rollout across two controllers.

## Gate

The device tests pass, and the gateway self-update and rollback are tested
on Linux and on a Raspberry Pi.
