// Advanced automation editor (DESIGN §10, SPEC §11.7) — React Flow.
// Lazy-loaded chunk. Nodes: Trigger, AND/OR gates, Conditions, Actions
// (light/group/scene/relay/MQTT/UDP/TCP/variable/delay/automation are all
// action nodes). Connections are validated while drawing (canConnect) and
// the whole graph again on save (fromGraph). Positions persist in `graph`.
import * as React from "react";
import {
  Background,
  Controls,
  Handle,
  Position,
  ReactFlow,
  addEdge,
  useEdgesState,
  useNodesState,
  type Connection,
  type Edge,
  type Node,
  type NodeProps,
} from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import { GitMerge, Play, Plus, SlidersHorizontal, Trash2, Zap } from "lucide-react";

import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { ActionFields, ConditionFields, TriggerFields, type Refs } from "./SimpleEditor";
import { ACTION_LABEL, TRIGGER_LABEL, canConnect, toGraph, type AutomationDocT, type FlowEdge, type FlowNode, type FlowNodeData } from "./model";

type N = Node<FlowNodeData>;

function label(d: FlowNodeData): { title: string; detail: string } {
  if (d.kind === "trigger" && d.trigger) return { title: "Trigger", detail: TRIGGER_LABEL[d.trigger.type] + (d.trigger.target ? ` · ${d.trigger.target}` : "") };
  if (d.kind === "and" || d.kind === "or") return { title: d.kind.toUpperCase(), detail: d.kind === "and" ? "all conditions" : "any condition" };
  if (d.kind === "condition" && d.condition) {
    const c = d.condition;
    const detail = c.type === "time_range" ? `${c.start}–${c.end}` : c.type === "day_of_week" ? c.days.join(" ") : `${c.source} ${c.id} ${c.field ?? ""} ${c.operator} ${c.value}`;
    return { title: "Condition", detail };
  }
  if (d.kind === "action" && d.action) return { title: ACTION_LABEL[d.action.type], detail: d.action.target ?? (d.action.delay_ms ? `${d.action.delay_ms / 1000} s` : "") };
  return { title: d.kind, detail: "" };
}

function Box({ data, selected }: NodeProps<N>) {
  const { title, detail } = label(data);
  const kind = data.kind;
  const Icon = kind === "trigger" ? Zap : kind === "action" ? Play : kind === "condition" ? SlidersHorizontal : GitMerge;
  return (
    <div className={cn("w-52 rounded-lg border bg-card px-3 py-2 text-left shadow-sm", selected && "ring-2 ring-primary", kind === "trigger" && "border-primary/60")}>
      {kind !== "trigger" && <Handle type="target" position={kind === "condition" ? Position.Top : Position.Left} />}
      <div className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wide text-muted-foreground">
        <Icon className="size-3.5" aria-hidden="true" /> {title}
      </div>
      <p className="mt-1 truncate text-sm">{detail || "—"}</p>
      {(kind === "trigger" || kind === "action" || kind === "and" || kind === "or") && <Handle type="source" id="flow" position={Position.Right} />}
      {(kind === "and" || kind === "or") && <Handle type="source" id="cond" position={Position.Bottom} />}
    </div>
  );
}

const nodeTypes = { box: Box };

export interface FlowEditorHandle {
  graph: () => { nodes: FlowNode[]; edges: FlowEdge[] };
}

const FlowEditor = React.forwardRef<FlowEditorHandle, { doc: AutomationDocT; refs: Refs }>(function FlowEditor({ doc, refs }, ref) {
  const initial = React.useMemo(() => toGraph(doc), [doc]);
  const [nodes, setNodes, onNodesChange] = useNodesState<N>(initial.nodes.map((n) => ({ ...n, type: "box" })));
  const [edges, setEdges, onEdgesChange] = useEdgesState<Edge>(initial.edges.map((e) => ({ ...e, sourceHandle: e.sourceHandle ?? undefined })));
  const [selected, setSelected] = React.useState<string | null>(null);
  const counter = React.useRef(100);

  React.useImperativeHandle(ref, () => ({
    graph: () => ({
      nodes: nodes.map((n) => ({ id: n.id, position: n.position, data: n.data })),
      edges: edges.map((e) => ({ id: e.id, source: e.source, target: e.target, sourceHandle: e.sourceHandle })),
    }),
  }), [nodes, edges]);

  const byId = React.useMemo(() => new Map(nodes.map((n) => [n.id, n])), [nodes]);
  const isValidConnection = React.useCallback(
    (c: Connection | Edge) => {
      const s = byId.get(c.source);
      const t = byId.get(c.target);
      return !!s && !!t && c.source !== c.target && canConnect(s.data, t.data, c.sourceHandle);
    },
    [byId],
  );

  const add = (data: FlowNodeData) => {
    const id = `n${counter.current++}`;
    const maxX = Math.max(0, ...nodes.map((n) => n.position.x));
    setNodes((ns) => [...ns, { id, type: "box", position: { x: maxX + 60, y: 160 + (ns.length % 4) * 30 }, data }]);
    setSelected(id);
  };
  const update = (id: string, data: FlowNodeData) => setNodes((ns) => ns.map((n) => (n.id === id ? { ...n, data } : n)));
  const sel = selected ? byId.get(selected) : undefined;

  return (
    <div className="grid gap-4 lg:grid-cols-[1fr_20rem]">
      <div className="space-y-2">
        <div className="flex flex-wrap gap-2" role="toolbar" aria-label="Add node">
          <Button size="sm" variant="outline" onClick={() => add({ kind: "and" })}><Plus className="size-3" /> AND</Button>
          <Button size="sm" variant="outline" onClick={() => add({ kind: "or" })}><Plus className="size-3" /> OR</Button>
          <Button size="sm" variant="outline" onClick={() => add({ kind: "condition", condition: { type: "time_range", start: "18:00", end: "23:00" } })}><Plus className="size-3" /> Condition</Button>
          <Button size="sm" variant="outline" onClick={() => add({ kind: "action", action: { type: "light_on", target: "" } })}><Plus className="size-3" /> Action</Button>
          <Button size="sm" variant="outline" onClick={() => add({ kind: "action", action: { type: "delay", delay_ms: 5000 } })}><Plus className="size-3" /> Delay</Button>
        </div>
        <div className="h-[28rem] rounded-xl border bg-card" aria-label="Automation graph">
          <ReactFlow
            nodes={nodes}
            edges={edges}
            nodeTypes={nodeTypes}
            onNodesChange={onNodesChange}
            onEdgesChange={onEdgesChange}
            onConnect={(c) => setEdges((es) => addEdge({ ...c, id: `${c.source}-${c.sourceHandle}-${c.target}` }, es))}
            isValidConnection={isValidConnection}
            onSelectionChange={({ nodes: ns }) => setSelected(ns[0]?.id ?? null)}
            fitView
            deleteKeyCode={["Backspace", "Delete"]}
            proOptions={{ hideAttribution: true }}
          >
            <Background gap={16} />
            <Controls showInteractive={false} />
          </ReactFlow>
        </div>
        <p className="text-xs text-muted-foreground">
          Drag from a node's right edge to the next step. Gates (AND/OR) take conditions from their bottom handle. Select a node to edit it; Delete removes it.
        </p>
      </div>
      <aside className="space-y-3 rounded-xl border bg-card p-4" aria-label="Node properties">
        {!sel ? (
          <p className="text-sm text-muted-foreground">Select a node to edit its settings.</p>
        ) : (
          <>
            <div className="flex items-center justify-between">
              <h3 className="font-semibold">{label(sel.data).title}</h3>
              {sel.data.kind !== "trigger" && (
                <Button variant="ghost" size="icon" aria-label="Delete node" onClick={() => {
                  setNodes((ns) => ns.filter((n) => n.id !== sel.id));
                  setEdges((es) => es.filter((e) => e.source !== sel.id && e.target !== sel.id));
                  setSelected(null);
                }}><Trash2 className="size-4" /></Button>
              )}
            </div>
            <div className="[&_.grid]:grid-cols-1">
              {sel.data.kind === "trigger" && sel.data.trigger && <TriggerFields trigger={sel.data.trigger} refs={refs} onChange={(t) => update(sel.id, { ...sel.data, trigger: t })} />}
              {sel.data.kind === "condition" && sel.data.condition && <ConditionFields condition={sel.data.condition} refs={refs} onChange={(c) => update(sel.id, { ...sel.data, condition: c })} />}
              {sel.data.kind === "action" && sel.data.action && <ActionFields action={sel.data.action} refs={refs} onChange={(a) => update(sel.id, { ...sel.data, action: a })} />}
              {(sel.data.kind === "and" || sel.data.kind === "or") && (
                <div className="flex gap-2">
                  <Button size="sm" variant={sel.data.kind === "and" ? "default" : "outline"} onClick={() => update(sel.id, { kind: "and" })}>AND</Button>
                  <Button size="sm" variant={sel.data.kind === "or" ? "default" : "outline"} onClick={() => update(sel.id, { kind: "or" })}>OR</Button>
                </div>
              )}
            </div>
            {sel.data.kind === "condition" && (
              <div className="flex flex-wrap gap-1">
                {(["time_range", "day_of_week", "state_comparison"] as const).map((t) => (
                  <Button key={t} size="xs" variant="outline" onClick={() => update(sel.id, { ...sel.data, condition: t === "time_range" ? { type: t, start: "18:00", end: "23:00" } : t === "day_of_week" ? { type: t, days: ["mon", "tue", "wed", "thu", "fri"] } : { type: t, source: "light", id: "", field: "power", operator: "eq", value: 1 } })}>
                    {t.replace("_", " ")}
                  </Button>
                ))}
              </div>
            )}
          </>
        )}
      </aside>
    </div>
  );
});

export default FlowEditor;
