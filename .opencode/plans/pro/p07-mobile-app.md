# P7 — Mobile app (React Native, Expo)

The app starts only after the P4–P6 gates have passed, so the API and
identity model are stable (brief §17).

## Outcome

An Android (and iOS) app covering the brief's §18 "Mobile" list. It:

- signs in;
- manages homes and rooms;
- onboards controllers (hotspot or LAN) and gateways;
- controls every light type with the controls each light supports;
- runs scenes;
- shows a live state and a route badge (Local / Remote / Unavailable).

## Tasks

- [ ] `mobile/`:
  - Expo, with the SDK pinned at start;
  - TypeScript strict;
  - Expo Router;
  - ESLint and Prettier;
  - Vitest/Jest with React Native Testing Library.

  The design tokens follow DESIGN.md's palette, adapted to native.
- [ ] `src/api/`:
  - one fetch wrapper that unwraps the envelope;
  - Zod models per endpoint, tested against `protocol/fixtures` and the
    OpenAPI examples;
  - TanStack Query hooks.
- [ ] Auth:
  - the access token is held in memory and the refresh token in
    `expo-secure-store`;
  - a single-flight refresh on 401;
  - logout clears everything.
- [ ] Live state:
  - an events WebSocket client with reconnect and a resync of queries
    after a reconnect;
  - a Zustand store for live device state;
  - pending commands are tracked by command ID until the result arrives
    (brief §12.5).
- [ ] Local route (`src/local/`):
  - discovery: Android NSD/mDNS through an Expo module, plus UDP
    `LUMEN?`;
  - per-device local tickets in secure storage, refreshed in the
    background;
  - a local REST and WS client using the controller's existing API;
  - a route selector with a reachability probe of `GET /system/info`
    within 800 ms;
  - when the route changes, live sources are swapped without losing
    pending commands.

  Expo config plugins are needed for native modules (mDNS, UDP, Wi-Fi
  join), so the app uses an Expo development build, not Expo Go.
- [ ] Onboarding:
  1. Scan the QR or label, or pick a device found on the LAN.
  2. Join the controller's setup hotspot (Android `WifiNetworkSpecifier`,
     iOS `NEHotspotConfiguration`), or use the LAN.
  3. Sign in to the controller. On a fresh device, the app runs the forced
     admin password change and the user sets the password.
  4. Configure the controller's Wi-Fi.
  5. Choose the home's gateway. If the home has none, the app first
     runs gateway onboarding.
  6. Get a controller claim token and call `/cloud/pair` on the
     controller.
  7. Wait for `device.online`.
  8. Assign a room.

  Gateway onboarding: LAN discovery `_lumen-gw._tcp` plus the setup code.
- [ ] Screens:
  - auth: login, register, forgot password;
  - home switcher and dashboard: rooms with lights, scenes;
  - light sheet: power, brightness, colour wheel, white channel and colour
    temperature, effects, each only if the light supports it;
  - room and group controls;
  - scenes;
  - devices: status, firmware, diagnostics summary;
  - gateways and their children;
  - members (read-only in P7);
  - settings.
- [ ] Accessibility: labels, minimum touch targets of 44 pt, dynamic type,
      reduced motion. Dark and light themes.

## Tests

- **Unit/component:** the API layer, route selection, pending-command
  logic and the light controls per type.
- **E2E (Maestro):** login, control, scene, against the compose cloud plus
  simgateway.
- **Phone, on a real Android device:**
  - the full onboarding of a gateway and then a fresh KC868-AP;
  - local versus remote switching (Wi-Fi off and back on);
  - offline home internet with local control still working.

## Gate

- The tests pass.
- An Android release build installs.
- The phone checklist is passed and logged.
- iOS is recorded as untested if no device is available.
