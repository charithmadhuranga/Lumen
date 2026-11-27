# Lumen Gateway

Go service for a Linux mini PC (Raspberry Pi and similar). It is the home's hub and the **only** path from
Lumen controllers to Lumen Cloud (docs/pro/DECISIONS.md D-17, D-18). Plan: `.opencode/plans/pro/p06-gateway.md`.

Status: **P0 skeleton.** Implemented so far: `internal/ticket` (local-ticket mint and verify, checked against
`protocol/fixtures/tickets.json`). Everything else in the P6 design is not started.

```sh
export PATH="$HOME/.local/opt/go/bin:$PATH"   # Go 1.27.4 (pinned in go.mod)
go vet ./... && go test -race ./...
```
