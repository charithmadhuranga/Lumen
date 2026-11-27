# P2 — Accounts, homes, members, rooms

## Outcome

A user can register, sign in, refresh, sign out, and manage homes and
cloud rooms. Every route is authorised by home role. The API is documented
by OpenAPI.

## Tasks

- [ ] Tables:
  - `users`: email in lower case, unique; argon2id hash; status.
  - `refresh_tokens`: family ID, hash, parent, expiry, revoked.
  - `homes`: name, IANA timezone, owner.
  - `home_members`: home, user, role; unique (home, user).
  - `rooms`: home, name, sort order.
- [ ] Auth routes:
  - `POST /auth/register`, `/auth/login`, `/auth/refresh`, `/auth/logout`;
    `GET /auth/me`.
  - The access token is a JWT (HS256 with a rotating key ID, 15 min) with
    claims `sub` and `sid`.
  - Refresh tokens are opaque, 256-bit, stored hashed, rotate on every
    use, and last 30 days.
  - Reusing an already-rotated token revokes the whole family.
- [ ] Password rules: at least 10 characters; reject the email itself.
      Rate limits: 5 failed logins per IP and email per 5 min, plus a
      lockout like the controller's. Login takes about the same time
      whether the user exists or not.
- [ ] Homes: CRUD. The owner is the first member.
- [ ] Rooms: CRUD within a home.
- [ ] Members: list them, change a role (owner or admin only), remove a
      member, leave a home. The last owner cannot leave.
- [ ] Role matrix in `app/core/permissions.py`, used by a dependency
      everywhere:
  - owner: all,
  - admin: all except deleting the home or transferring ownership,
  - member: control, scenes, read,
  - guest: control only.
- [ ] Tenant isolation: every query is scoped through home membership. A
      test asserts that a user from home A gets 404, not 403, for any
      resource of home B.
- [ ] Email verification and password reset are stubbed behind an
      `EmailSender` interface with a log-only development implementation.
      The real provider comes in P10. Sign-in works before verification;
      claiming a device requires a verified email from P11.
- [ ] `docs/pro/CLOUD_API.md` generated from OpenAPI (a script), plus a
      hand-written auth section.

## Tests

- The token lifecycle: rotation, reuse detection, expiry and logout.
- The role matrix as a parametrised test over every route.
- Tenant isolation, rate limiting, and validation errors.

## Gate

- Tests pass, lint is clean, and the migration upgrades and downgrades
  cleanly.
- The OpenAPI document is generated and committed.
