# P6 — Lumen Gateway (Go, Linux)

## Outcome

`lumen-gateway`, a single binary installed from a `.deb` on a Raspberry
Pi. It is the home's only cloud connection (DECISIONS D-17) and its hub
(D-18). It:

- is claimed from the app;
- discovers and adopts the controllers on its LAN;
- bridges their commands, state, config and availability to the cloud;
- manages the fleet: health, restart, rename, nightly config backups;
- keeps every LAN feature working through internet outages.

The brief's firmware cloud modules live here, in Go:
`CloudConnectionManager` and `CloudMqttService` (`internal/cloud`),
`CloudCommandRouter` and `CloudStatePublisher` (`internal/bridge`), and
`CloudOfflineQueue` (`internal/buffer`).

## Design

```text
cmd/lumen-gateway        main: config, signals, wiring
internal/config          /etc/lumen-gateway/config.toml + state dir /var/lib/lumen-gateway (0700)
internal/store           state: identity, MQTT credential, children {deviceId, controllerId, localKey, lastIP}
                         as a JSON file written atomically (tmp + fsync + rename); secrets file 0600
internal/protocol        Go types for PROTOCOL.md + fixture tests
internal/ticket          LT1 mint/verify (fixture vectors)
internal/cloud           MQTT client (paho.golang, MQTT 5, TLS), registration HTTP client
internal/link            ControllerLink: REST client + WS client per controller, reconnect, IP follow
internal/discovery       UDP LUMEN? broadcast + mDNS browse
internal/bridge          maps cmd → REST, WS → state/config/availability; dedup ring; result mapping
internal/buffer          bounded offline ring for events/telemetry (1000 entries / 1 MB)
internal/fleet           health polling (/system/diagnostics every 60 s), restart, rename, adoptable-controller list
internal/backup          nightly GET /backup per controller, 14-day retention, restore on request
internal/localapi        :8780 LAN HTTP: /local/v1/register (controller claim), /local/v1/setup (gateway claim), /healthz
internal/update          (P9) signed self-update
```

Dependencies (pinned in `go.mod`, licences checked):

- `github.com/eclipse/paho.golang` (EPL-2.0/EDL-1.0; MQTT 5),
- `github.com/coder/websocket` (ISC),
- `github.com/grandcat/zeroconf` (MIT; mDNS),
- `github.com/BurntSushi/toml` (MIT).

Everything else uses the standard library.

## Tasks

- [ ] Project skeleton:
  - logging with `log/slog` (JSON; secrets never logged);
  - graceful shutdown on SIGTERM;
  - systemd `Type=notify` watchdog.
- [ ] Setup:
  - On first start with no identity, generate an 8-character setup code.
  - Print it to the journal and to `lumen-gateway setup` (CLI, also as a
    terminal QR code), and advertise it over mDNS as
    `_lumen-gw._tcp` with `state=unclaimed`.
  - `POST /local/v1/setup {claimToken}` (the setup proof checked by the
    cloud) → `/gateway/register` → store the identity → connect.
- [ ] Cloud link: TLS (system CAs, plus an optional custom CA for
      development), LWT on `g/{gw}/availability`, and subscriptions to
      `g/{gw}/children` and `d/{child}/cmd` for each assigned child. On
      assignment changes, subscriptions are added or removed.
- [ ] Controller registration (`POST /local/v1/register`):
  - validates the request;
  - calls the cloud's `children/register` with the controller's claim
    token;
  - stores `localKey`;
  - replies to the controller;
  - rate limit of 5 per minute;
  - accepts requests from RFC 1918 and link-local source addresses only.
- [ ] `ControllerLink`:
  - mints `g:` admin tickets of 12 h, renewed at 50 %;
  - keeps one WS connection with ping and reconnect, with backoff of
    1–60 s;
  - REST client with timeouts of 3 s and at most 4 requests in flight per
    controller;
  - follows IP changes through discovery.
- [ ] Bridge (PROTOCOL §9):
  - command checks (`exp`, dedup, assignment);
  - REST call;
  - result mapping;
  - availability with a 15 s debounce;
  - coalesced state;
  - `config/reported` built from the REST collections on `config_changed`;
  - startup republish.
- [ ] Offline buffer and replay; commands are never buffered.
- [ ] Fleet management (D-18 §1):
  - an adoptable-controller list (discovered, `cloud=off`) reported in
    `g/{gw}/status`;
  - health summaries per child published as `telemetry` (firmware,
    uptime, free/min heap, RSSI, peripheral errors);
  - `device.restart` mapped to `POST /system/restart`.
- [ ] Config backups (D-18 §2):
  - a nightly `GET /backup` per child into `/var/lib/lumen-gateway/backups/`
    (0700, 14 days, checksum);
  - events on failure;
  - restore through a gateway op `backup.restore` (admin, explicit
    confirmation in the app).

  The protocol entry is added in this phase (PROTOCOL.md + schema).
- [ ] NTP: the gateway runs as an SNTP source for its LAN only if the owner
      enables it (default off; the controllers' SNTP server setting is
      left as it is).
- [ ] Packaging:
  - `goreleaser`-style `Makefile` targets building `.deb` packages for
    arm64, armhf and amd64;
  - a `lumen-gateway` system user;
  - a systemd unit with hardening: `ProtectSystem=strict`,
    `NoNewPrivileges` and `CapabilityBoundingSet=` empty, plus
    `AmbientCapabilities` for nothing (it binds to port 8780 > 1024).

## Tests

- **Unit:**
  - ticket vectors;
  - protocol fixtures;
  - bridge mapping, using a fake controller (`httptest` REST and WS that
    imitate the firmware);
  - dedup and expiry;
  - buffer bounds;
  - store atomicity, by simulating a crash between write and rename.
- **Integration, against the P4 compose cloud plus fake controllers:**
  claim → children → command → `applied` → state, on both sides.
- **Device:**
  - a Raspberry Pi (or this Linux machine at first, recorded as "host
    Linux") plus a real KC868-AP in gateway mode;
  - remote command latency;
  - pull the controller's power, then restore it: availability and
    republish;
  - pull the gateway's WAN: local behaviour unchanged, replay after
    recovery;
  - reboot the gateway.

## Gate

- `go vet`, `staticcheck` (if available) and `go test -race ./...` pass.
- The three `.deb` packages build.
- The device tests pass with a real controller. The Raspberry Pi result is
  recorded separately from the "host Linux" result.
