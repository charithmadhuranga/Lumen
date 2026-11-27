# P2 — Accounts, homes, members, rooms

## Outcome

A user can register, sign in, refresh, sign out, and manage homes and
cloud rooms. Every route is authorised by home role. The API is documented
by OpenAPI.

## Tasks

- [x] Tables:
  - `users`: email in lower case, unique; argon2id hash; status.
  - `refresh_tokens`: family ID, hash, parent, expiry, revoked.
  - `homes`: name, IANA timezone, owner.
  - `home_members`: home, user, role; unique (home, user).
  - `rooms`: home, name, sort order.
- [x] Auth routes:
  - `POST /auth/register`, `/auth/login`, `/auth/refresh`, `/auth/logout`;
    `GET /auth/me`.
  - The access token is a JWT (HS256 with a rotating key ID, 15 min) with
    claims `sub` and `sid`.
  - Refresh tokens are opaque, 256-bit, stored hashed, rotate on every
    use, and last 30 days.
  - Reusing an already-rotated token revokes the whole family.
- [x] Password rules: at least 10 characters; reject the email itself.
      Rate limits: 5 failed logins per IP and email per 5 min, plus a
      lockout like the controller's. Login takes about the same time
      whether the user exists or not.
- [x] Homes: CRUD. The owner is the first member.
- [x] Rooms: CRUD within a home.
- [x] Members: list them, change a role (owner or admin only), remove a
      member, leave a home. The last owner cannot leave.
- [x] Role matrix in `app/core/permissions.py`, used by a dependency
      everywhere:
  - owner: all,
  - admin: all except deleting the home or transferring ownership,
  - member: control, scenes, read,
  - guest: control only.
- [x] Tenant isolation: every query is scoped through home membership. A
      test asserts that a user from home A gets 404, not 403, for any
      resource of home B.
- [x] Email verification and password reset are stubbed behind an
      `EmailSender` interface with a log-only development implementation.
      The real provider comes in P10. Sign-in works before verification;
      claiming a device requires a verified email from P11.
- [x] `docs/pro/CLOUD_API.md` generated from OpenAPI (a script), plus a
      hand-written auth section.

## Tests

- The token lifecycle: rotation, reuse detection, expiry and logout.
- The role matrix as a parametrised test over every route.
- Tenant isolation, rate limiting, and validation errors.

## Gate

- Tests pass, lint is clean, and the migration upgrades and downgrades
  cleanly.
- The OpenAPI document is generated and committed.

## Result (2026-11-27)

Gate passed. 151 cloud tests (116 new) against the compose Postgres/Redis; ruff and mypy --strict clean;
migration 0002 upgrades and downgrades cleanly and matches the models; OpenAPI generated
(`docs/pro/openapi.json`) and checked in CI; HTTP smoke test against the rebuilt container (register, login,
refresh, home, room, logout → 401) passed (local docker).

What was built beyond the task list: a `sessions` table (the access token's `sid`; every request checks it, so
sign-out, password change and revocation are immediate), `GET/DELETE /auth/sessions`, e-mail verification and
password reset implemented end to end with single-use hashed tokens (delivery is a development log sender
until P10), and `LUMEN_TRUSTED_PROXIES` so client IPs for rate limits cannot be spoofed with X-Forwarded-For.

Notes and limitations:
- The refresh-token "family" is the `sessions` row (refresh tokens point at it); "revoked" lives on the session.
- Guests can also read the home and its rooms (they must see what they control); they cannot see the member
  list.
- Members can only be added through invitations, which arrive in P10; until then multi-member homes exist only
  in tests.
- Refresh tokens rotate strictly: two concurrent refreshes with the same token end the session. The app must
  refresh single-flight (P7).
- The development JWT secret was lengthened to 39 characters (PyJWT warns below 32 bytes for HS256).
- Tests reset state with DELETE per table (TRUNCATE cost ~1 s per test).
- 423 `ACCOUNT_LOCKED` reveals that an account exists, but only after 10 failures that the caller caused.
