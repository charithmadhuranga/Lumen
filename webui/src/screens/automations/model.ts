// Automation document model shared by the Simple and Advanced editors
// (SPEC §11.5). Mirrors src/automation/automation_model.cpp; the firmware
// re-validates everything (references, loops) on save.
import { z } from "zod";

export const TRIGGER_TYPES = [
  "light_state",
  "brightness_change",
  "color_change",
  "input_state",
  "button_press",
  "double_press",
  "long_press",
  "time_schedule",
  "sunrise",
  "sunset",
  "sensor_threshold",
  "mqtt_event",
  "esp_now_event",
  "scene_activated",
  "group_state_changed",
  "controller_startup",
  "timer_expired",
] as const;

export const ACTION_TYPES = [
  "light_on",
  "light_off",
  "light_toggle",
  "light_brightness",
  "light_color",
  "light_cct",
  "scene_activate",
  "group_control",
  "room_control",
  "relay_toggle",
  "effect_run",
  "effect_stop",
  "delay",
  "mqtt_publish",
  "udp_send",
  "tcp_send",
  "execute_automation",
  "set_variable",
] as const;

export const OPERATORS = ["eq", "ne", "gt", "lt", "ge", "le"] as const;
export const DAYS = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"] as const;

export const Trigger = z.object({
  type: z.enum(TRIGGER_TYPES),
  target: z.string().optional(),
  state: z.boolean().optional(),
  threshold: z.number().optional(),
  operator: z.enum(OPERATORS).optional(),
  payload: z.string().optional(),
  time: z.string().optional(),
  days: z.array(z.enum(DAYS)).optional(),
  offset_min: z.number().optional(),
  interval_s: z.number().optional(),
});

export type ConditionT =
  | { type: "and" | "or" | "not"; items: ConditionT[] }
  | { type: "time_range"; start: string; end: string }
  | { type: "day_of_week"; days: (typeof DAYS)[number][] }
  | { type: "state_comparison"; source: "light" | "input" | "variable"; id: string; field?: string; operator: (typeof OPERATORS)[number]; value: number };

export type LeafConditionT = Exclude<ConditionT, { type: "and" | "or" | "not" }>;

export const Condition: z.ZodType<ConditionT> = z.lazy(() =>
  z.union([
    z.object({ type: z.enum(["and", "or", "not"]), items: z.array(Condition) }),
    z.object({ type: z.literal("time_range"), start: z.string(), end: z.string() }),
    z.object({ type: z.literal("day_of_week"), days: z.array(z.enum(DAYS)) }),
    z.object({
      type: z.literal("state_comparison"),
      source: z.enum(["light", "input", "variable"]),
      id: z.string(),
      field: z.string().optional(),
      operator: z.enum(OPERATORS),
      value: z.number(),
    }),
  ]),
);

export const Action = z.object({
  type: z.enum(ACTION_TYPES),
  target: z.string().optional(),
  power: z.boolean().optional(),
  brightness: z.number().optional(),
  red: z.number().optional(),
  green: z.number().optional(),
  blue: z.number().optional(),
  color_temperature: z.number().optional(),
  transition_ms: z.number().optional(),
  effect: z.string().optional(),
  speed_ms: z.number().optional(),
  delay_ms: z.number().optional(),
  topic: z.string().optional(),
  host: z.string().optional(),
  payload: z.string().optional(),
  port: z.number().optional(),
  retain: z.boolean().optional(),
  value: z.number().optional(),
  op: z.enum(["set", "add"]).optional(),
});

export const GraphNode = z.object({
  id: z.string(),
  kind: z.string(),
  x: z.number(),
  y: z.number(),
});
export const Graph = z.object({ version: z.literal(1), nodes: z.array(GraphNode) });

export const AutomationDoc = z.object({
  id: z.string(),
  name: z.string(),
  enabled: z.boolean(),
  mode: z.enum(["simple", "advanced"]).optional(),
  trigger: Trigger,
  conditions: z.object({ operator: z.enum(["AND", "OR", "NOT"]), items: z.array(Condition) }).optional(),
  actions: z.array(Action),
  execution: z
    .object({ cooldown_ms: z.number(), max_executions_per_hour: z.number(), allow_parallel: z.boolean() })
    .optional(),
  graph: Graph.optional(),
  summary: z.string().optional(),
  runtime: z.unknown().optional(),
});

export const Runtime = z.object({
  running: z.boolean(),
  runs: z.number(),
  errors: z.number(),
  lastResult: z.string(),
  lastRunUnix: z.number(),
});

export const AutomationSummary = z.object({
  id: z.string(),
  name: z.string(),
  enabled: z.boolean(),
  mode: z.string(),
  triggerType: z.string(),
  summary: z.string(),
  runtime: Runtime,
});

export const HistoryEntry = z.object({
  automationId: z.string(),
  trigger: z.string(),
  unixTime: z.number(),
  uptimeMs: z.number(),
  durationMs: z.number(),
  result: z.string(),
  message: z.string(),
});

export const AutomationSettings = z.object({
  hasLocation: z.boolean(),
  latitude: z.number(),
  longitude: z.number(),
  timeSynced: z.boolean(),
  localTime: z.string().optional(),
  sunrise: z.string().optional(),
  sunset: z.string().optional(),
});

export type TriggerT = z.infer<typeof Trigger>;
export type ActionT = z.infer<typeof Action>;
export type AutomationDocT = z.infer<typeof AutomationDoc>;
export type AutomationSummaryT = z.infer<typeof AutomationSummary>;
export type HistoryEntryT = z.infer<typeof HistoryEntry>;
export type AutomationSettingsT = z.infer<typeof AutomationSettings>;

export const TRIGGER_LABEL: Record<TriggerT["type"], string> = {
  light_state: "Light turns on/off",
  brightness_change: "Brightness changes",
  color_change: "Colour changes",
  input_state: "Input changes",
  button_press: "Button pressed",
  double_press: "Button double-pressed",
  long_press: "Button held",
  time_schedule: "At a time",
  sunrise: "At sunrise",
  sunset: "At sunset",
  sensor_threshold: "Variable crosses a threshold",
  mqtt_event: "MQTT message",
  esp_now_event: "Peer controller event",
  scene_activated: "Scene activated",
  group_state_changed: "Group changes",
  controller_startup: "Controller starts",
  timer_expired: "Repeating timer",
};

export const ACTION_LABEL: Record<ActionT["type"], string> = {
  light_on: "Turn light on",
  light_off: "Turn light off",
  light_toggle: "Toggle light",
  light_brightness: "Set brightness",
  light_color: "Set colour",
  light_cct: "Set colour temperature",
  scene_activate: "Activate scene",
  group_control: "Control group",
  room_control: "Control room",
  relay_toggle: "Toggle output",
  effect_run: "Run effect",
  effect_stop: "Stop effect",
  delay: "Wait",
  mqtt_publish: "Publish MQTT",
  udp_send: "Send UDP",
  tcp_send: "Send TCP",
  execute_automation: "Run automation",
  set_variable: "Set variable",
};

export function newAutomation(): AutomationDocT {
  return {
    id: "",
    name: "",
    enabled: true,
    mode: "simple",
    trigger: { type: "input_state", target: "", state: true },
    conditions: undefined,
    actions: [{ type: "light_on", target: "" }],
    execution: { cooldown_ms: 0, max_executions_per_hour: 60, allow_parallel: false },
  };
}

export function slugify(s: string): string {
  return s.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 40) || "automation";
}

/* ----------------------------------------------- advanced graph (flow) */

export type FlowKind = "trigger" | "and" | "or" | "condition" | "action";

export interface FlowNodeData {
  kind: FlowKind;
  trigger?: TriggerT;
  condition?: Exclude<ConditionT, { type: "and" | "or" | "not" }>;
  action?: ActionT;
  [key: string]: unknown;
}

export interface FlowNode {
  id: string;
  position: { x: number; y: number };
  data: FlowNodeData;
  type?: string;
}

export interface FlowEdge {
  id: string;
  source: string;
  target: string;
  sourceHandle?: string | null;
}

// Connection rules (validated while drawing and on save):
//   trigger --flow--> (and|or) | action
//   and/or  --flow--> action           (the action chain after the gate)
//   and/or  --cond--> condition | and | or   (gate inputs)
//   action  --flow--> action           (sequence)
export function canConnect(source: FlowNodeData, target: FlowNodeData, handle: string | null | undefined): boolean {
  if (handle === "cond") return (source.kind === "and" || source.kind === "or") && (target.kind === "condition" || target.kind === "and" || target.kind === "or");
  if (source.kind === "trigger") return target.kind === "action" || target.kind === "and" || target.kind === "or";
  if (source.kind === "and" || source.kind === "or") return target.kind === "action";
  if (source.kind === "action") return target.kind === "action";
  return false;
}

/** Model -> graph (auto layout when no stored positions). */
export function toGraph(doc: AutomationDocT): { nodes: FlowNode[]; edges: FlowEdge[] } {
  const stored = new Map((doc.graph?.nodes ?? []).map((n) => [n.id, n]));
  const pos = (id: string, x: number, y: number) => {
    const s = stored.get(id);
    return s ? { x: s.x, y: s.y } : { x, y };
  };
  const nodes: FlowNode[] = [{ id: "trigger", position: pos("trigger", 0, 0), data: { kind: "trigger", trigger: doc.trigger } }];
  const edges: FlowEdge[] = [];
  let last = "trigger";
  if (doc.conditions && doc.conditions.items.length) {
    let counter = 0;
    const build = (c: ConditionT, depth: number, row: number): string => {
      const id = `c${counter++}`;
      if (c.type === "and" || c.type === "or" || c.type === "not") {
        nodes.push({ id, position: pos(id, 260, row * 110 - depth * 40), data: { kind: c.type === "or" ? "or" : "and" } });
        c.items.forEach((child, i) => {
          const cid = build(child, depth + 1, row + i);
          edges.push({ id: `${id}-${cid}`, source: id, target: cid, sourceHandle: "cond" });
        });
      } else {
        nodes.push({ id, position: pos(id, 260 + 240 * depth, 160 + row * 110), data: { kind: "condition", condition: c as LeafConditionT } });
      }
      return id;
    };
    const root = { type: doc.conditions.operator === "OR" ? ("or" as const) : ("and" as const), items: doc.conditions.items };
    const gate = build(root, 0, 0);
    nodes.find((n) => n.id === gate)!.position = pos(gate, 260, 0);
    edges.push({ id: `trigger-${gate}`, source: "trigger", target: gate, sourceHandle: "flow" });
    last = gate;
  }
  doc.actions.forEach((a, i) => {
    const id = `a${i}`;
    nodes.push({ id, position: pos(id, 520 + i * 240, 0), data: { kind: "action", action: a } });
    edges.push({ id: `${last}-${id}`, source: last, target: id, sourceHandle: "flow" });
    last = id;
  });
  return { nodes, edges };
}

/** Graph -> model fields. Returns errors when the graph is not a valid pipeline. */
export function fromGraph(nodes: FlowNode[], edges: FlowEdge[]): {
  trigger?: TriggerT;
  conditions?: AutomationDocT["conditions"];
  actions: ActionT[];
  graph: z.infer<typeof Graph>;
  errors: string[];
} {
  const errors: string[] = [];
  const byId = new Map(nodes.map((n) => [n.id, n]));
  const triggers = nodes.filter((n) => n.data.kind === "trigger");
  if (triggers.length !== 1) errors.push("Use exactly one trigger node");
  for (const e of edges) {
    const s = byId.get(e.source);
    const t = byId.get(e.target);
    if (!s || !t || !canConnect(s.data, t.data, e.sourceHandle)) errors.push("A connection is not allowed (check the node types)");
  }
  const flowOut = (id: string) => edges.filter((e) => e.source === id && e.sourceHandle !== "cond");
  const condOut = (id: string) => edges.filter((e) => e.source === id && e.sourceHandle === "cond");
  const graph = { version: 1 as const, nodes: nodes.map((n) => ({ id: n.id, kind: n.data.kind, x: Math.round(n.position.x), y: Math.round(n.position.y) })) };
  if (errors.length) return { actions: [], graph, errors };

  const trigger = triggers[0];
  let conditions: AutomationDocT["conditions"];
  const actions: ActionT[] = [];
  const visited = new Set<string>();

  const condOf = (id: string, depth: number): ConditionT | null => {
    if (depth > 4) {
      errors.push("Conditions are nested too deeply");
      return null;
    }
    const n = byId.get(id)!;
    if (visited.has(id)) {
      errors.push("Condition wiring contains a cycle");
      return null;
    }
    visited.add(id);
    if (n.data.kind === "condition") return n.data.condition ?? null;
    const items = condOut(id).map((e) => condOf(e.target, depth + 1)).filter((x): x is ConditionT => x !== null);
    if (items.length === 0) errors.push(`${n.data.kind.toUpperCase()} gate has no conditions connected`);
    return { type: n.data.kind === "or" ? "or" : "and", items };
  };

  const first = flowOut(trigger.id);
  if (first.length > 1) errors.push("The trigger can only connect to one next step");
  let cursor = first[0]?.target;
  if (cursor && (byId.get(cursor)!.data.kind === "and" || byId.get(cursor)!.data.kind === "or")) {
    const gate = condOf(cursor, 0) as { type: "and" | "or"; items: ConditionT[] } | null;
    if (gate) conditions = { operator: gate.type === "or" ? "OR" : "AND", items: gate.items };
    const next = flowOut(cursor);
    if (next.length > 1) errors.push("A gate can only connect to one action");
    cursor = next[0]?.target;
  }
  const seen = new Set<string>();
  while (cursor) {
    if (seen.has(cursor)) {
      errors.push("The action chain loops back on itself");
      break;
    }
    seen.add(cursor);
    const n = byId.get(cursor)!;
    if (n.data.kind !== "action" || !n.data.action) {
      errors.push("Only actions can follow in the chain");
      break;
    }
    actions.push(n.data.action);
    const next = flowOut(cursor);
    if (next.length > 1) errors.push("Actions run in sequence: one outgoing connection per action");
    cursor = next[0]?.target;
  }
  if (actions.length === 0) errors.push("Connect at least one action after the trigger");
  const orphans = nodes.filter((n) => n.data.kind === "action" && !seen.has(n.id));
  if (orphans.length) errors.push(`${orphans.length} action node(s) are not connected`);
  return { trigger: trigger.data.trigger, conditions, actions, graph, errors };
}

/** Plain-language summary (DESIGN §7.6). */
export function describe(doc: AutomationDocT, name: (kind: string, id?: string) => string): string {
  const t = doc.trigger;
  const target = name(t.type.includes("press") || t.type === "input_state" ? "input" : t.type.startsWith("scene") ? "scene" : t.type.startsWith("group") ? "group" : "light", t.target);
  const when: Record<TriggerT["type"], string> = {
    light_state: `${target} turns ${t.state === false ? "off" : t.state ? "on" : "on or off"}`,
    brightness_change: `${target} brightness changes`,
    color_change: `${target} colour changes`,
    input_state: `${target} turns ${t.state === false ? "off" : t.state ? "on" : "on or off"}`,
    button_press: `${target} is pressed`,
    double_press: `${target} is double-pressed`,
    long_press: `${target} is held`,
    time_schedule: `it is ${t.time ?? "--:--"}`,
    sunrise: t.offset_min ? `${Math.abs(t.offset_min)} min ${t.offset_min < 0 ? "before" : "after"} sunrise` : "the sun rises",
    sunset: t.offset_min ? `${Math.abs(t.offset_min)} min ${t.offset_min < 0 ? "before" : "after"} sunset` : "the sun sets",
    sensor_threshold: `${t.target ?? "?"} crosses ${t.threshold ?? 0}`,
    mqtt_event: `an MQTT message arrives on ${t.target ?? "?"}`,
    esp_now_event: "a peer controller sends an event",
    scene_activated: `scene ${target} is activated`,
    group_state_changed: `group ${target} changes`,
    controller_startup: "the controller starts",
    timer_expired: `every ${t.interval_s ?? 60} s`,
  };
  const days = t.days && t.days.length < 7 ? ` on ${t.days.join(", ")}` : "";
  const cond = doc.conditions?.items.length ? `, if ${doc.conditions.items.length} condition${doc.conditions.items.length > 1 ? "s" : ""} ${doc.conditions.operator === "OR" ? "(any)" : "(all)"} match` : "";
  const acts = doc.actions
    .slice(0, 3)
    .map((a) => {
      const n = name(a.type.startsWith("scene") ? "scene" : a.type.startsWith("group") ? "group" : a.type.startsWith("room") ? "room" : "light", a.target);
      switch (a.type) {
        case "light_on": return `turn on ${n}${a.brightness !== undefined ? ` at ${a.brightness}%` : ""}`;
        case "light_off": return `turn off ${n}`;
        case "light_toggle": return `toggle ${n}`;
        case "light_brightness": return `set ${n} to ${a.brightness ?? 0}%`;
        case "light_color": return `colour ${n}`;
        case "light_cct": return `set ${n} to ${a.color_temperature ?? 0} K`;
        case "scene_activate": return `activate ${n}`;
        case "delay": return `wait ${Math.round((a.delay_ms ?? 0) / 1000)} s`;
        default: return ACTION_LABEL[a.type].toLowerCase() + (a.target ? ` ${n}` : "");
      }
    })
    .join(", then ");
  return `When ${when[t.type]}${days}${cond}: ${acts}${doc.actions.length > 3 ? " …" : ""}.`;
}
