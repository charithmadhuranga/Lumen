# Lumen Pro protocol contract (v1)

Machine-readable form of [docs/pro/PROTOCOL.md](../docs/pro/PROTOCOL.md).

| Path | What |
|---|---|
| `schemas/*.schema.json` | JSON Schema 2020-12, one per message; `common.schema.json` holds shared definitions |
| `fixtures/<schema>/valid/*.json` | messages every implementation must accept |
| `fixtures/<schema>/invalid/<rule>.json` | messages every implementation must reject; the file name is the rule broken |
| `fixtures/tickets.json` | local-ticket vectors: key, tickets, expected verdict and reason |
| `tools/make_fixtures.py` | generates `fixtures/` (edit the examples there, never the JSON by hand) |
| `tools/ticket_ref.py` | reference ticket verifier; runs the vectors |

Availability payloads are the plain strings `online` / `offline`, not JSON.

Checks: `npm run validate:schemas` (repo root) and `python3 protocol/tools/ticket_ref.py`.
Consumers load the same fixtures in their own tests: cloud (pytest), gateway
(`go test`), firmware (host tests), mobile app (unit tests).

A protocol change = PROTOCOL.md + schemas + `make_fixtures.py` (regenerate) +
every consumer, in one commit.
