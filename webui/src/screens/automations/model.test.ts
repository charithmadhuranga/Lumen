// Automation graph validation suite (AGENTS.md §11): round trips and
// invalid wiring.
import { describe, expect, it } from "vitest";

import { canConnect, fromGraph, toGraph, type AutomationDocT, type FlowEdge, type FlowNode } from "./model";

const doc: AutomationDocT = {
  id: "kitchen",
  name: "Kitchen",
  enabled: true,
  trigger: { type: "input_state", target: "di1", state: true },
  conditions: {
    operator: "AND",
    items: [
      { type: "time_range", start: "18:00", end: "23:00" },
      { type: "or", items: [{ type: "day_of_week", days: ["sat"] }, { type: "state_comparison", source: "light", id: "a", field: "power", operator: "eq", value: 0 }] },
    ],
  },
  actions: [
    { type: "light_on", target: "a", brightness: 80 },
    { type: "delay", delay_ms: 5000 },
    { type: "light_off", target: "a" },
  ],
};

describe("automation graph", () => {
  it("round-trips model -> graph -> model", () => {
    const g = toGraph(doc);
    const back = fromGraph(g.nodes, g.edges);
    expect(back.errors).toEqual([]);
    expect(back.trigger).toEqual(doc.trigger);
    expect(back.actions).toEqual(doc.actions);
    expect(back.conditions).toEqual(doc.conditions);
    expect(back.graph.version).toBe(1);
  });

  it("keeps stored node positions", () => {
    const g = toGraph({ ...doc, graph: { version: 1, nodes: [{ id: "a0", kind: "action", x: 999, y: 7 }] } });
    expect(g.nodes.find((n) => n.id === "a0")?.position).toEqual({ x: 999, y: 7 });
  });

  it("rejects invalid connections", () => {
    const trig = { kind: "trigger" as const };
    const act = { kind: "action" as const };
    const cond = { kind: "condition" as const };
    const and = { kind: "and" as const };
    expect(canConnect(trig, act, "flow")).toBe(true);
    expect(canConnect(trig, cond, "flow")).toBe(false); // conditions hang off gates
    expect(canConnect(act, trig, "flow")).toBe(false);
    expect(canConnect(and, cond, "cond")).toBe(true);
    expect(canConnect(cond, act, "flow")).toBe(false);
  });

  it("reports graphs that are not a single pipeline", () => {
    const g = toGraph(doc);
    // Two triggers.
    const twoTriggers: FlowNode[] = [...g.nodes, { id: "t2", position: { x: 0, y: 0 }, data: { kind: "trigger", trigger: { type: "controller_startup" } } }];
    expect(fromGraph(twoTriggers, g.edges).errors.join()).toMatch(/exactly one trigger/);
    // Branching action chain.
    const branch: FlowEdge[] = [...g.edges, { id: "x", source: "a0", target: "a2", sourceHandle: "flow" }];
    expect(fromGraph(g.nodes, branch).errors.join()).toMatch(/one outgoing connection/);
    // Cycle back into the chain.
    const cycle: FlowEdge[] = [...g.edges, { id: "y", source: "a2", target: "a0", sourceHandle: "flow" }];
    expect(fromGraph(g.nodes, cycle).errors.join()).toMatch(/loop|outgoing/);
    // Orphan action.
    const orphan: FlowNode[] = [...g.nodes, { id: "lonely", position: { x: 0, y: 0 }, data: { kind: "action", action: { type: "light_on", target: "b" } } }];
    expect(fromGraph(orphan, g.edges).errors.join()).toMatch(/not connected/);
    // Disallowed edge type.
    const bad: FlowEdge[] = [...g.edges, { id: "z", source: "a0", target: "trigger", sourceHandle: "flow" }];
    expect(fromGraph(g.nodes, bad).errors.join()).toMatch(/not allowed/);
  });
});
