# Phase 10 — Networking

**SPEC §26 Phase 10, SPEC §13–15, §14; DESIGN §7.9–7.10** ·
**Skill:** `lumen-networking`

## Outcome

Wi-Fi (STA/AP/fallback), Ethernet, MQTT with Home Assistant discovery, TCP,
UDP and the RS485 transport all work **independently** — and a network
failure never stops local lighting or automation.

## Dependencies

Phase 1 (event bus), Phase 2 (Ethernet/UART drivers), Phase 6 (web stack for
config screens), Phase 9 (MQTT/UDP/TCP action hooks become real).

## Tasks

### 10.1 Network manager and Wi-Fi/Ethernet

- [ ] `network_manager.*` + `wifi_manager.*`: STA (SSID/password, DHCP or
      static IP/gateway/DNS, reconnection with backoff, status events), AP
      (unique device identity + secure password or explicit provisioning —
      **never an unauthenticated permanent hotspot**, AGENTS.md §8),
      AP+STA combined, configurable fallback policy when STA fails
      (SPEC §15.1–15.3).
- [ ] `ethernet_driver.*`: PHY profiles from the board profile (LAN8720,
      addr 0, MDC 23, MDIO 18, clock GPIO17_OUT, power -1 for KC868-AP);
      init only when the profile declares Ethernet capability; link status
      events; graceful degradation when the PHY fails to init.
- [ ] Network endpoints: `GET /network`, `PUT /network/wifi`,
      `PUT /network/ap`, `PUT /network/ethernet`, `GET /network/status`.

### 10.2 MQTT (`mqtt_manager.*`)

- [ ] ESP-MQTT: broker host/port, username/password (stored in **NVS**,
      never in `integrations/mqtt.json` — write-only API fields),
      TLS option, client ID, QoS, keepalive, retained state, LWT
      (`lighting/{controller_id}/status` → offline), automatic
      reconnection, topic prefix (SPEC §13.1).
- [ ] Topic convention from SPEC §13.2: status, `light/{id}/set` +
      `/state`, `group/{id}/set`, `scene/{id}/activate`, `event`,
      `automation/{id}/status`. JSON payloads validated on both directions;
      command payloads enqueue like any other producer.
- [ ] State publication only **after** hardware application; retained
      messages refreshed on reconnect.
- [ ] Endpoints: `GET /integrations`, `PUT /integrations/mqtt`,
      `POST /integrations/mqtt/test`.

### 10.3 Home Assistant discovery (`ha_discovery.*`)

- [ ] Discovery for dimmers, RGB, RGBW, CCT, relay lights, supported groups
      and diagnostics per SPEC §13.3–13.4; stable `unique_id`s; correct
      schema/optional properties per light type.
- [ ] Regenerate entity configs when the logical device changes; publish
      retained-deletion for removed/renamed entities per lifecycle policy.
- [ ] Availability tied to LWT topic.

### 10.4 TCP and UDP

- [ ] `tcp_server.*`: server + client, persistent connections, **explicit
      message framing** (a read is not a message — length-prefix or delimiter
      protocol, versioned), JSON commands, authentication handshake,
      heartbeats, timeouts, multiple clients, reconnection (SPEC §14.1).
- [ ] `udp_manager.*`: discovery, broadcast/multicast, lighting events, state
      announcements, low-latency control — **control disabled or
      authenticated by default** on untrusted networks; packet validation
      and rate limiting (SPEC §14.2).
- [ ] Endpoints: `PUT /integrations/tcp`, `PUT /integrations/udp`.

### 10.5 RS485 transport

- [ ] UART driver configured from profile + Modbus RTU framing over it
      (SPEC §2.2), direction-control behavior per Phase 3's measurement.
      Expose as a transport adapter, not a lighting dependency.

### 10.6 Web UI screens (DESIGN §7.9–7.10)

- [ ] **Integrations:** cards for MQTT, Home Assistant, ESP-NOW, TCP, UDP —
      enable switch, settings form, **Test connection** where applicable.
      Secrets write-only: show "•••••• (set)" + Replace; never returned by
      the API.
- [ ] **Network:** Wi-Fi (scan,SSID,password,DHCP/static), AP, Ethernet,
      status; confirm dialog for changes that may drop the connection.
- [ ] `espnow` integration settings placeholder card appears only in Phase 11
      (do not ship a fake control).

### 10.7 Tests

- [ ] Host: topic construction/payload publish (mock client), HA discovery
      config generation per light type incl. cleanup, TCP framing parser
      (fragmented/coalesced/oversized reads), UDP validation + rate limit.
- [ ] Integration: MQTT command → light change → state published; discovery
      visible to a broker test harness; Wi-Fi fallback behavior; Ethernet
      init (hardware or marked mock).
- [ ] **Offline test:** kill broker + Wi-Fi → local lights, scenes and
      automations keep working; MQTT resumes on return (SPEC §12.6).
- [ ] Playwright: integrations/network forms against mock API; axe.

## Contracts & interfaces introduced

- MQTT topic + payload contracts (documented in `docs/API.md` or a new
  `docs/MQTT.md` — update the doc index accordingly).
- TCP framing spec (versioned) — document alongside in `docs/API.md`.
- Integration config documents (secrets excluded by construction).

## Gate (acceptance criteria)

1. All protocols work independently; disabling any one does not affect the
   others.
2. Network failure does not stop local lighting (measured, documented).
3. HA discovers every supported light type with stable unique IDs; entity
   cleanup verified on delete/rename.
4. TCP framing survives fragmented and coalesced reads (tests).
5. UDP control is authenticated/disabled by default; rate limiter tested.
6. Secrets never appear in API responses, logs or exports (test).
7. UI: integrations/network screens pass lint/test/axe; budgets hold.

## Docs to update

`docs/API.md` (new endpoints), `docs/MQTT.md` or API doc (topics, discovery),
`docs/UI_UX.md`, `docs/ARCHITECTURE.md` (networking tasks),
`docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- TLS memory cost on ESP32: measure heap with TLS broker enabled before
  committing; may need to document a heap-based feature trade-off.
- Wi-Fi provisioning security (first-run AP password strategy) must be
  decided with the user before implementation (AGENTS.md §15).
