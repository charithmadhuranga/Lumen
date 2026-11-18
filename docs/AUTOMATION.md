# Automation Engine

> **Not yet implemented** (Phase 0 stub — Phase 9 fills this file).
>
> Planned for: SPEC §11 — trigger/condition/action engine, local execution
> (no internet, no Home Assistant), safety rules from AGENTS.md §7:
> recursion/loop prevention, bounded queues, execution time and rate limits,
> cancellation, no user-supplied JavaScript or shell.
>
> Reference schema already in place: `schemas/automation.schema.json`
> (validates today; runtime engine lands in Phase 9).

## Architecture

(EVENT → trigger matcher → condition evaluator → action executor → event/log)

## Trigger types

SPEC §11.2 — light/brightness/color change, input state, button variants,
time schedule, sunrise/sunset, sensor threshold, MQTT event, ESP-NOW event,
scene activated, group changed, controller startup, timer expired.

## Condition operators

SPEC §11.3 — comparisons, AND/OR/NOT, time range, day of week, state
comparison.

## Safety limits

(Phase 9: cooldown, max executions per hour, parallel prohibition, graph
validation, loop detection)

## Editor

Simple form editor + advanced React Flow editor (SPEC §11.6–11.7).
