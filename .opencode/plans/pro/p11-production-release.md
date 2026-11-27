# P11 — Hardening, production, factory provisioning, pilot

## Tasks

### Security

- [ ] Threat model document (`docs/pro/SECURITY.md`): assets, actors and
      trust boundaries (phone, cloud, broker, gateway, LAN, controller).
      It covers every boundary's authentication, and abuse cases.
- [ ] External dependency audit: `pip-audit`, `govulncheck`, `npm audit`,
      licence report.
- [ ] Rate limits on every public cloud route, and size limits on every
      body.
- [ ] Security headers.
- [ ] Brute-force tests.
- [ ] Penetration-test checklist.
- [ ] Credential rotation:
  - `POST /device/rotate` over MQTT, as a `config.apply` of kind `cloud`
    carrying a new secret through an authenticated channel;
  - KEK rotation for `local_key_enc`.
- [ ] Gateway: a key-exchange upgrade for `/local/v1/register`, so
      `K_local` never crosses the LAN in plaintext (X25519 + HKDF +
      AES-GCM; mbedTLS supports X25519). Local HTTPS on the controller is
      evaluated against flash and RAM.
- [ ] Firmware Secure Boot V2 and flash encryption: a written procedure
      and a recovery drill on a sacrificial board. Enabled only on the
      owner's decision (irreversible eFuses).

### Production

- [ ] Infrastructure as code for the chosen provider (owner decision:
      cloud provider and region):
  - managed Postgres with point-in-time recovery and backups (tested
    restore);
  - EMQX with TLS;
  - object storage;
  - secrets manager;
  - TLS certificates (ACME);
  - Prometheus and Grafana dashboards and alerts (broker connections,
    command latency, error rates, DB).
- [ ] The broker licence decision is resolved (D-12).
- [ ] Staging and production environments; deploys via CI with
      migrations.

### Factory provisioning (for Lumen-branded hardware)

- [ ] Manufacturing tool for Lumen-branded gateways and controllers:
  - gateways get a per-device ECDSA key and certificate (mTLS to EMQX,
    signed by a manufacturing CA held offline) and a printed setup code;
  - controllers get a device ID and a claim code on a QR label.
- [ ] Cloud: pre-registered devices are claimed by QR code; mTLS
      authentication on EMQX for gateways alongside password
      authentication.

### Pilot and release gate

- [ ] Pilot rig as in brief §16.4, with a Raspberry Pi gateway. All 12
      scenarios from the brief are run and recorded.
- [ ] 72 h soak: a gateway with 3 children and the app connected. No
      reboots, flat controller heap, stable gateway memory, and no missed
      state.
- [ ] Release gate: the brief's §19 items 1–14, each with evidence.
