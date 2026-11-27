# P10 — Notifications, history, audit, invitations

## Tasks

- [ ] Email provider behind `EmailSender`, which the owner picks: Amazon
      SES, Postmark or similar. Ask before adding it. Verification and
      password-reset emails.
- [ ] Member invitations: `POST /homes/{home}/invitations` sends an email
      with a single-use token valid for 7 days. Accepting it joins the
      home with the role chosen in the invitation.
- [ ] Push notifications:
  - Expo Push (FCM/APNs underneath);
  - a device token table per user and installation;
  - per-home preferences;
  - events that trigger a push: device offline for more than 5 min,
    device back online, OTA done or failed, gateway offline, and
    automation failures (from `events`).
- [ ] Audit log: every authenticated cloud mutation (actor, home, action,
      target, IP, time), kept for 1 year. `GET /homes/{home}/audit`,
      owner and admin only.
- [ ] Event history in the app: device events and command history per
      device.

## Gate

- The tests pass.
- A push is received on a real phone.
- The invitation flow passes end to end with the development email sink.
