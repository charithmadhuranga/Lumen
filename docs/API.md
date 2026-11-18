# API Reference

> **Not yet implemented** (Phase 0 stub — later phases fill this file
> instead of creating a new one).
>
> Planned for: SPEC §17 (Web Server and API) — REST endpoints under `/api/v1`,
> the `{success, data, error}` response envelope, WebSocket live events at
> `/api/v1/ws`, authentication/session rules.
>
> Current state: no web server in the firmware yet. The web UI dev proxy
> forwards `/api` to `LUMEN_CONTROLLER_URL` (default `http://127.0.0.1:8080`)
> so the endpoint shape can be developed in Phase 6 against mocks first.

## Endpoints

(Phase 6/7)

## WebSocket messages

(Phase 6)

## Conventions

- Every payload is parsed with Zod on the client (AGENTS.md §9).
- Version prefix: `/api/v1`.
