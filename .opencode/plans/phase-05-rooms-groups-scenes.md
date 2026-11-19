# Phase 5 — Rooms, Groups and Scenes

**SPEC §26 Phase 5, SPEC §9** · **Skill:** `lumen-lighting-engine`

## Outcome

Lights can be organized into rooms and groups, groups aggregate correctly
across mixed light types, and scenes save and restore lighting states with
transitions. All of it persists transactionally.

## Dependencies

Phase 4 (channels + engine API), Phase 1 (config persistence).

## Tasks

### 5.1 Room manager (`src/lighting/room_manager.*`)

- [x] Room model (SPEC §9.1): id, name, floor, icon, channel ids, group
      references, floor-plan references (consumed in Phase 8).
- [x] CRUD persisted to `/config/rooms/rooms.json`; referential integrity —
      deleting a room detaches its lights without deleting them.
- [x] Room-level operations: master power, master brightness, scene
      shortcuts (fan-out to channels through the engine API).

### 5.2 Group manager (`src/lighting/group_manager.*`)

- [x] Groups may span rooms; operations: on/off, brightness, RGB, CCT,
      effect, scene activation (SPEC §9.2).
- [x] **Aggregation policy for mixed types** — defined, tested behavior:
      - brightness change adjusts every member's brightness, preserving each
        member's color;
      - RGB/CCT set applies only to members that support it;
      - on/off applies to all;
      - group state reported as an aggregate (on-count / mean brightness)
        for the UI.
- [x] Group commands fan out through the engine with a shared `command_id`
      so Phase 11 can treat them as one logical command.

### 5.3 Scene manager (`src/lighting/scene_manager.*`)

- [x] Scene JSON exactly per SPEC §9.3 (id, name, transition_ms, states[]).
- [x] Operations: save current state, manual edit, activate (with per-scene
      transition), duplicate, export/import JSON (validated), delete.
- [x] Activation fans out to the engine as one coordinated command batch;
      scenes validate at save time — unknown light ids rejected, no partial
      broken scenes persisted.
- [x] Scene activation publishes `SCENE_ACTIVATED` on the bus (automation
      trigger + WS event later).

### 5.4 Persistence and events

- [x] All three documents carry `schema_version` + config version, go
      through the transactional config store, survive reboot (round-trip
      test).
- [x] Bus events: `ROOM_CHANGED`, `GROUP_STATE_CHANGED`, `SCENE_ACTIVATED`.

## Contracts & interfaces introduced

- Engine fan-out API: `apply_scene(scene_id, transition_ms, origin)`,
  `group_command(group_id, patch, origin)`.
- REST resource shapes for rooms/groups/scenes (Zod schemas land with the
  Phase 6 API; keep field names stable now).

## Gate (acceptance criteria)

1. Lights organized into rooms; room master power/brightness behaves per the ✅
   aggregation policy (tests for dimmer-only, RGB-only and mixed groups).
2. Scenes restore saved states including color, with the configured ✅
   transition (host test against engine mock).
3. Save → reboot → identical rooms/groups/scenes (round-trip). ✅
4. Invalid scene (unknown light, wrong property for type) rejected at save. ✅
5. Export/import round-trip with validation. ✅
6. Deleting a room/group/light never cascades destructively (orphan handling ✅
   tested).

## Docs to update

`docs/API.md` (room/group/scene payloads), `docs/AUTOMATION.md` (scene
activated event, used as a trigger in Phase 9), `docs/IMPLEMENTATION_STATUS.md`.

## Risks / open questions

- Group aggregation semantics for `cct` vs `rgb` members in one group must be
  written down as tests first — ambiguity here becomes UI bugs later.
