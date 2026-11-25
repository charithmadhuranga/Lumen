// Simple automation editor (DESIGN §7.6): Trigger, optional Conditions,
// Actions, Execution — every field type-specific, pickers populated from
// the controller's real lights/inputs/scenes/groups/rooms.
import { ArrowDown, ArrowUp, Plus, Trash2 } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Switch } from "@/components/ui/switch";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Field } from "@/components/common";
import {
  ACTION_LABEL,
  ACTION_TYPES,
  DAYS,
  OPERATORS,
  TRIGGER_LABEL,
  TRIGGER_TYPES,
  type ActionT,
  type AutomationDocT,
  type ConditionT,
  type TriggerT,
} from "./model";

export interface Refs {
  lights: { id: string; name: string }[];
  inputs: { id: string; name: string }[];
  scenes: { id: string; name: string }[];
  groups: { id: string; name: string }[];
  rooms: { id: string; name: string }[];
  automations: { id: string; name: string }[];
  outputs: { id: string; name: string }[];
  effects: string[];
}

function Pick({ label, value, options, onChange }: { label: string; value: string | undefined; options: { id: string; name: string }[]; onChange: (v: string) => void }) {
  return (
    <Field label={label}>
      <Select value={value || undefined} onValueChange={onChange}>
        <SelectTrigger aria-label={label}><SelectValue placeholder={options.length ? "Choose…" : "None available"} /></SelectTrigger>
        <SelectContent>
          {options.map((o) => <SelectItem key={o.id} value={o.id}>{o.name}</SelectItem>)}
        </SelectContent>
      </Select>
    </Field>
  );
}

function Num({ label, value, onChange, min, max, step = 1, hint }: { label: string; value: number | undefined; onChange: (v: number) => void; min?: number; max?: number; step?: number; hint?: string }) {
  return (
    <Field label={label} hint={hint}>
      <Input type="number" inputMode="decimal" value={value ?? ""} min={min} max={max} step={step} aria-label={label}
        onChange={(e) => onChange(e.target.value === "" ? 0 : Number(e.target.value))} />
    </Field>
  );
}

function OnOff({ label, value, onChange }: { label: string; value: boolean | undefined; onChange: (v: boolean | undefined) => void }) {
  return (
    <Field label={label}>
      <Select value={value === undefined ? "any" : value ? "on" : "off"} onValueChange={(v) => onChange(v === "any" ? undefined : v === "on")}>
        <SelectTrigger aria-label={label}><SelectValue /></SelectTrigger>
        <SelectContent>
          <SelectItem value="on">Turns on</SelectItem>
          <SelectItem value="off">Turns off</SelectItem>
          <SelectItem value="any">Either</SelectItem>
        </SelectContent>
      </Select>
    </Field>
  );
}

function Days({ value, onChange }: { value: string[] | undefined; onChange: (v: (typeof DAYS)[number][]) => void }) {
  const set = new Set(value ?? DAYS);
  return (
    <fieldset className="space-y-1.5">
      <legend className="text-sm font-medium">Days</legend>
      <div className="flex flex-wrap gap-1">
        {DAYS.map((d) => (
          <button key={d} type="button" aria-pressed={set.has(d)}
            className={`min-h-11 min-w-11 rounded-md border px-2 text-sm capitalize ${set.has(d) ? "border-primary bg-primary/15" : "text-muted-foreground"}`}
            onClick={() => {
              const next = new Set(set);
              if (next.has(d)) next.delete(d);
              else next.add(d);
              if (next.size) onChange(DAYS.filter((x) => next.has(x)));
            }}>
            {d}
          </button>
        ))}
      </div>
    </fieldset>
  );
}

const OP_LABEL: Record<(typeof OPERATORS)[number], string> = { eq: "=", ne: "≠", gt: ">", lt: "<", ge: "≥", le: "≤" };

export function TriggerFields({ trigger, onChange, refs }: { trigger: TriggerT; onChange: (t: TriggerT) => void; refs: Refs }) {
  const set = (p: Partial<TriggerT>) => onChange({ ...trigger, ...p });
  const t = trigger.type;
  return (
    <div className="grid gap-3 sm:grid-cols-2">
      <Field label="When">
        <Select value={t} onValueChange={(v) => onChange({ type: v as TriggerT["type"], state: v === "light_state" || v === "input_state" ? true : undefined, time: v === "time_schedule" ? "18:00" : undefined, interval_s: v === "timer_expired" ? 60 : undefined })}>
          <SelectTrigger aria-label="Trigger type"><SelectValue /></SelectTrigger>
          <SelectContent>{TRIGGER_TYPES.map((x) => <SelectItem key={x} value={x}>{TRIGGER_LABEL[x]}</SelectItem>)}</SelectContent>
        </Select>
      </Field>
      {["light_state", "brightness_change", "color_change"].includes(t) && <Pick label="Light" value={trigger.target} options={refs.lights} onChange={(v) => set({ target: v })} />}
      {["input_state", "button_press", "double_press", "long_press"].includes(t) && <Pick label="Input" value={trigger.target} options={refs.inputs} onChange={(v) => set({ target: v })} />}
      {t === "scene_activated" && <Pick label="Scene" value={trigger.target} options={refs.scenes} onChange={(v) => set({ target: v })} />}
      {t === "group_state_changed" && <Pick label="Group" value={trigger.target} options={refs.groups} onChange={(v) => set({ target: v })} />}
      {(t === "light_state" || t === "input_state") && <OnOff label="Change" value={trigger.state} onChange={(v) => set({ state: v })} />}
      {t === "brightness_change" && (
        <>
          <Field label="Only when it becomes">
            <Select value={trigger.operator ?? "any"} onValueChange={(v) => set(v === "any" ? { operator: undefined, threshold: undefined } : { operator: v as TriggerT["operator"], threshold: trigger.threshold ?? 50 })}>
              <SelectTrigger aria-label="Threshold comparison"><SelectValue /></SelectTrigger>
              <SelectContent>
                <SelectItem value="any">Any change</SelectItem>
                {OPERATORS.map((o) => <SelectItem key={o} value={o}>{OP_LABEL[o]} threshold</SelectItem>)}
              </SelectContent>
            </Select>
          </Field>
          {trigger.operator && <Num label="Threshold (%)" value={trigger.threshold} min={0} max={100} onChange={(v) => set({ threshold: v })} />}
        </>
      )}
      {t === "time_schedule" && (
        <Field label="Time" htmlFor="trig-time">
          <Input id="trig-time" type="time" value={trigger.time ?? ""} onChange={(e) => set({ time: e.target.value })} />
        </Field>
      )}
      {(t === "sunrise" || t === "sunset") && <Num label="Offset (minutes, negative = before)" value={trigger.offset_min} min={-240} max={240} onChange={(v) => set({ offset_min: v })} />}
      {(t === "time_schedule" || t === "sunrise" || t === "sunset") && <Days value={trigger.days} onChange={(d) => set({ days: d.length === 7 ? undefined : d })} />}
      {t === "timer_expired" && <Num label="Every (seconds)" value={trigger.interval_s} min={10} max={86400} onChange={(v) => set({ interval_s: v })} />}
      {t === "sensor_threshold" && (
        <>
          <Field label="Variable" htmlFor="trig-var"><Input id="trig-var" value={trigger.target ?? ""} maxLength={24} onChange={(e) => set({ target: e.target.value })} /></Field>
          <Field label="Comparison">
            <Select value={trigger.operator ?? "gt"} onValueChange={(v) => set({ operator: v as TriggerT["operator"] })}>
              <SelectTrigger aria-label="Comparison"><SelectValue /></SelectTrigger>
              <SelectContent>{OPERATORS.map((o) => <SelectItem key={o} value={o}>{OP_LABEL[o]}</SelectItem>)}</SelectContent>
            </Select>
          </Field>
          <Num label="Threshold" value={trigger.threshold} step={0.1} onChange={(v) => set({ threshold: v })} />
        </>
      )}
      {t === "mqtt_event" && (
        <>
          <Field label="Topic" htmlFor="trig-topic"><Input id="trig-topic" value={trigger.target ?? ""} onChange={(e) => set({ target: e.target.value })} /></Field>
          <Field label="Payload (optional exact match)" htmlFor="trig-payload"><Input id="trig-payload" value={trigger.payload ?? ""} onChange={(e) => set({ payload: e.target.value || undefined })} /></Field>
        </>
      )}
      {t === "esp_now_event" && (
        <Field label="Event name (optional)" htmlFor="trig-peer"><Input id="trig-peer" value={trigger.target ?? ""} onChange={(e) => set({ target: e.target.value || undefined })} /></Field>
      )}
    </div>
  );
}

type Leaf = Exclude<ConditionT, { type: "and" | "or" | "not" }>;

export function ConditionFields({ condition, onChange, refs }: { condition: Leaf; onChange: (c: Leaf) => void; refs: Refs }) {
  if (condition.type === "time_range") {
    return (
      <div className="grid grid-cols-2 gap-3">
        <Field label="From" htmlFor="c-start"><Input id="c-start" type="time" value={condition.start} onChange={(e) => onChange({ ...condition, start: e.target.value })} /></Field>
        <Field label="Until" htmlFor="c-end"><Input id="c-end" type="time" value={condition.end} onChange={(e) => onChange({ ...condition, end: e.target.value })} /></Field>
      </div>
    );
  }
  if (condition.type === "day_of_week") {
    return <Days value={condition.days} onChange={(d) => onChange({ ...condition, days: d })} />;
  }
  const opts = condition.source === "light" ? refs.lights : condition.source === "input" ? refs.inputs : [];
  return (
    <div className="grid gap-3 sm:grid-cols-4">
      <Field label="Compare">
        <Select value={condition.source} onValueChange={(v) => onChange({ ...condition, source: v as Leaf extends { source: infer S } ? S : never, id: "", field: v === "light" ? "power" : undefined, value: v === "variable" ? 0 : 1 } as Leaf)}>
          <SelectTrigger aria-label="Compare what"><SelectValue /></SelectTrigger>
          <SelectContent>
            <SelectItem value="light">Light</SelectItem>
            <SelectItem value="input">Input</SelectItem>
            <SelectItem value="variable">Variable</SelectItem>
          </SelectContent>
        </Select>
      </Field>
      {condition.source === "variable" ? (
        <Field label="Variable" htmlFor="c-var"><Input id="c-var" value={condition.id} maxLength={24} onChange={(e) => onChange({ ...condition, id: e.target.value })} /></Field>
      ) : (
        <Pick label={condition.source === "light" ? "Light" : "Input"} value={condition.id} options={opts} onChange={(v) => onChange({ ...condition, id: v })} />
      )}
      {condition.source === "light" && (
        <Field label="Property">
          <Select value={condition.field ?? "power"} onValueChange={(v) => onChange({ ...condition, field: v, value: v === "power" ? 1 : 50, operator: v === "power" ? "eq" : "ge" })}>
            <SelectTrigger aria-label="Property"><SelectValue /></SelectTrigger>
            <SelectContent>
              <SelectItem value="power">Power</SelectItem>
              <SelectItem value="brightness">Brightness</SelectItem>
            </SelectContent>
          </Select>
        </Field>
      )}
      {(condition.source === "input" || condition.field === "power") && condition.source !== "variable" ? (
        <Field label="Is">
          <Select value={condition.value ? "on" : "off"} onValueChange={(v) => onChange({ ...condition, operator: "eq", value: v === "on" ? 1 : 0 })}>
            <SelectTrigger aria-label="State"><SelectValue /></SelectTrigger>
            <SelectContent>
              <SelectItem value="on">On</SelectItem>
              <SelectItem value="off">Off</SelectItem>
            </SelectContent>
          </Select>
        </Field>
      ) : (
        <div className="grid grid-cols-2 gap-2">
          <Field label="Is">
            <Select value={condition.operator} onValueChange={(v) => onChange({ ...condition, operator: v as Leaf extends { operator: infer O } ? O : never } as Leaf)}>
              <SelectTrigger aria-label="Operator"><SelectValue /></SelectTrigger>
              <SelectContent>{OPERATORS.map((o) => <SelectItem key={o} value={o}>{OP_LABEL[o]}</SelectItem>)}</SelectContent>
            </Select>
          </Field>
          <Num label="Value" value={condition.value} step={condition.source === "variable" ? 0.1 : 1} onChange={(v) => onChange({ ...condition, value: v })} />
        </div>
      )}
    </div>
  );
}

export function ActionFields({ action, onChange, refs }: { action: ActionT; onChange: (a: ActionT) => void; refs: Refs }) {
  const set = (p: Partial<ActionT>) => onChange({ ...action, ...p });
  const t = action.type;
  const lightTypes = ["light_on", "light_off", "light_toggle", "light_brightness", "light_color", "light_cct", "effect_run", "effect_stop"];
  return (
    <div className="grid gap-3 sm:grid-cols-3">
      <Field label="Do">
        <Select value={t} onValueChange={(v) => onChange({ type: v as ActionT["type"], target: lightTypes.includes(v) && lightTypes.includes(t) ? action.target : undefined, delay_ms: v === "delay" ? 5000 : undefined, brightness: v === "light_brightness" ? 50 : undefined, color_temperature: v === "light_cct" ? 3000 : undefined, power: v === "group_control" || v === "room_control" ? true : undefined, red: v === "light_color" ? 255 : undefined, green: v === "light_color" ? 160 : undefined, blue: v === "light_color" ? 60 : undefined })}>
          <SelectTrigger aria-label="Action type"><SelectValue /></SelectTrigger>
          <SelectContent>{ACTION_TYPES.map((x) => <SelectItem key={x} value={x}>{ACTION_LABEL[x]}</SelectItem>)}</SelectContent>
        </Select>
      </Field>
      {lightTypes.includes(t) && <Pick label="Light" value={action.target} options={refs.lights} onChange={(v) => set({ target: v })} />}
      {t === "scene_activate" && <Pick label="Scene" value={action.target} options={refs.scenes} onChange={(v) => set({ target: v })} />}
      {t === "group_control" && <Pick label="Group" value={action.target} options={refs.groups} onChange={(v) => set({ target: v })} />}
      {t === "room_control" && <Pick label="Room" value={action.target} options={refs.rooms} onChange={(v) => set({ target: v })} />}
      {t === "relay_toggle" && <Pick label="Output or switch" value={action.target} options={[...refs.lights, ...refs.outputs]} onChange={(v) => set({ target: v })} />}
      {t === "execute_automation" && <Pick label="Automation" value={action.target} options={refs.automations} onChange={(v) => set({ target: v })} />}
      {(t === "group_control" || t === "room_control") && (
        <Field label="Power">
          <Select value={action.power === false ? "off" : "on"} onValueChange={(v) => set({ power: v === "on" })}>
            <SelectTrigger aria-label="Power"><SelectValue /></SelectTrigger>
            <SelectContent><SelectItem value="on">On</SelectItem><SelectItem value="off">Off</SelectItem></SelectContent>
          </Select>
        </Field>
      )}
      {(t === "light_brightness" || t === "light_on" || t === "group_control" || t === "room_control" || t === "effect_run") && (
        <Num label={t === "light_brightness" ? "Brightness (%)" : "Brightness (%, optional)"} value={action.brightness} min={0} max={100} onChange={(v) => set({ brightness: v })} />
      )}
      {t === "light_color" && (
        <Field label="Colour" htmlFor="act-colour">
          <input id="act-colour" type="color" className="h-10 w-full cursor-pointer rounded-md border bg-transparent"
            value={`#${[action.red ?? 0, action.green ?? 0, action.blue ?? 0].map((x) => x.toString(16).padStart(2, "0")).join("")}`}
            onChange={(e) => {
              const n = parseInt(e.target.value.slice(1), 16);
              set({ red: (n >> 16) & 255, green: (n >> 8) & 255, blue: n & 255 });
            }} />
        </Field>
      )}
      {t === "light_cct" && <Num label="Colour temperature (K)" value={action.color_temperature} min={1000} max={20000} step={50} onChange={(v) => set({ color_temperature: v })} />}
      {t === "effect_run" && (
        <Field label="Effect">
          <Select value={action.effect || undefined} onValueChange={(v) => set({ effect: v })}>
            <SelectTrigger aria-label="Effect"><SelectValue placeholder="Choose…" /></SelectTrigger>
            <SelectContent>{refs.effects.map((e) => <SelectItem key={e} value={e}>{e.replace("_", " ")}</SelectItem>)}</SelectContent>
          </Select>
        </Field>
      )}
      {["light_on", "light_off", "light_brightness", "light_color", "light_cct"].includes(t) && (
        <Num label="Fade (ms, optional)" value={action.transition_ms} min={0} max={600000} step={100} onChange={(v) => set({ transition_ms: v })} />
      )}
      {t === "delay" && <Num label="Wait (seconds)" value={(action.delay_ms ?? 0) / 1000} min={0} max={3600} step={0.5} onChange={(v) => set({ delay_ms: Math.round(v * 1000) })} />}
      {t === "mqtt_publish" && (
        <>
          <Field label="Topic" htmlFor="act-topic"><Input id="act-topic" value={action.topic ?? ""} onChange={(e) => set({ topic: e.target.value })} /></Field>
          <Field label="Payload" htmlFor="act-payload"><Input id="act-payload" value={action.payload ?? ""} maxLength={512} onChange={(e) => set({ payload: e.target.value })} /></Field>
        </>
      )}
      {(t === "udp_send" || t === "tcp_send") && (
        <>
          <Field label="Host" htmlFor="act-host"><Input id="act-host" value={action.host ?? ""} onChange={(e) => set({ host: e.target.value })} /></Field>
          <Num label="Port" value={action.port} min={1} max={65535} onChange={(v) => set({ port: v })} />
          <Field label="Payload" htmlFor="act-np"><Input id="act-np" value={action.payload ?? ""} maxLength={512} onChange={(e) => set({ payload: e.target.value })} /></Field>
        </>
      )}
      {t === "set_variable" && (
        <>
          <Field label="Variable" htmlFor="act-var"><Input id="act-var" value={action.target ?? ""} maxLength={24} onChange={(e) => set({ target: e.target.value })} /></Field>
          <Field label="Operation">
            <Select value={action.op ?? "set"} onValueChange={(v) => set({ op: v as "set" | "add" })}>
              <SelectTrigger aria-label="Operation"><SelectValue /></SelectTrigger>
              <SelectContent><SelectItem value="set">Set to</SelectItem><SelectItem value="add">Add</SelectItem></SelectContent>
            </Select>
          </Field>
          <Num label="Value" value={action.value} step={0.1} onChange={(v) => set({ value: v })} />
        </>
      )}
    </div>
  );
}

export function SimpleEditor({ doc, onChange, refs }: { doc: AutomationDocT; onChange: (d: AutomationDocT) => void; refs: Refs }) {
  const conds = (doc.conditions?.items ?? []) as ConditionT[];
  const nested = conds.some((c) => c.type === "and" || c.type === "or" || c.type === "not");
  const setConds = (items: ConditionT[], operator = doc.conditions?.operator ?? "AND") =>
    onChange({ ...doc, conditions: items.length ? { operator, items } : undefined });
  const setAction = (i: number, a: ActionT) => onChange({ ...doc, actions: doc.actions.map((x, j) => (j === i ? a : x)) });
  const move = (i: number, d: -1 | 1) => {
    const a = [...doc.actions];
    const [x] = a.splice(i, 1);
    a.splice(i + d, 0, x);
    onChange({ ...doc, actions: a });
  };
  const exec = doc.execution ?? { cooldown_ms: 0, max_executions_per_hour: 60, allow_parallel: false };

  return (
    <div className="space-y-6">
      <section className="space-y-3 rounded-xl border bg-card p-4" aria-labelledby="s-trigger">
        <h2 id="s-trigger" className="font-semibold">1. Trigger</h2>
        <TriggerFields trigger={doc.trigger} onChange={(t) => onChange({ ...doc, trigger: t })} refs={refs} />
      </section>

      <section className="space-y-3 rounded-xl border bg-card p-4" aria-labelledby="s-cond">
        <div className="flex flex-wrap items-center gap-2">
          <h2 id="s-cond" className="mr-auto font-semibold">2. Conditions <span className="text-sm font-normal text-muted-foreground">(optional)</span></h2>
          {conds.length > 1 && (
            <Select value={doc.conditions?.operator === "OR" ? "OR" : "AND"} onValueChange={(v) => setConds(conds, v as "AND" | "OR")}>
              <SelectTrigger className="w-44" aria-label="Combine conditions"><SelectValue /></SelectTrigger>
              <SelectContent><SelectItem value="AND">All must match</SelectItem><SelectItem value="OR">Any may match</SelectItem></SelectContent>
            </Select>
          )}
        </div>
        {nested ? (
          <p className="text-sm text-muted-foreground">These conditions use nested AND/OR groups. Edit them in the Advanced editor.</p>
        ) : (
          <>
            {conds.map((c, i) => (
              <div key={i} className="flex items-start gap-2 rounded-lg border p-3">
                <div className="flex-1">
                  <ConditionFields condition={c as Leaf} refs={refs} onChange={(nc) => setConds(conds.map((x, j) => (j === i ? nc : x)))} />
                </div>
                <Button variant="ghost" size="icon" aria-label="Remove condition" onClick={() => setConds(conds.filter((_, j) => j !== i))}><Trash2 className="size-4" /></Button>
              </div>
            ))}
            <div className="flex flex-wrap gap-2">
              <Button size="sm" variant="outline" onClick={() => setConds([...conds, { type: "time_range", start: "18:00", end: "23:00" }])}><Plus className="size-3" /> Time window</Button>
              <Button size="sm" variant="outline" onClick={() => setConds([...conds, { type: "day_of_week", days: ["mon", "tue", "wed", "thu", "fri"] }])}><Plus className="size-3" /> Days</Button>
              <Button size="sm" variant="outline" onClick={() => setConds([...conds, { type: "state_comparison", source: "light", id: "", field: "power", operator: "eq", value: 1 }])}><Plus className="size-3" /> Light / input state</Button>
            </div>
          </>
        )}
      </section>

      <section className="space-y-3 rounded-xl border bg-card p-4" aria-labelledby="s-act">
        <h2 id="s-act" className="font-semibold">3. Actions <span className="text-sm font-normal text-muted-foreground">(run in order)</span></h2>
        {doc.actions.map((a, i) => (
          <div key={i} className="flex items-start gap-2 rounded-lg border p-3">
            <span className="mt-8 w-5 text-sm text-muted-foreground">{i + 1}.</span>
            <div className="flex-1"><ActionFields action={a} refs={refs} onChange={(na) => setAction(i, na)} /></div>
            <div className="flex flex-col">
              <Button variant="ghost" size="icon" aria-label="Move up" disabled={i === 0} onClick={() => move(i, -1)}><ArrowUp className="size-4" /></Button>
              <Button variant="ghost" size="icon" aria-label="Move down" disabled={i === doc.actions.length - 1} onClick={() => move(i, 1)}><ArrowDown className="size-4" /></Button>
              <Button variant="ghost" size="icon" aria-label="Remove action" disabled={doc.actions.length === 1} onClick={() => onChange({ ...doc, actions: doc.actions.filter((_, j) => j !== i) })}><Trash2 className="size-4" /></Button>
            </div>
          </div>
        ))}
        <Button size="sm" variant="outline" disabled={doc.actions.length >= 32} onClick={() => onChange({ ...doc, actions: [...doc.actions, { type: "light_on", target: "" }] })}><Plus className="size-3" /> Add action</Button>
      </section>

      <section className="space-y-3 rounded-xl border bg-card p-4" aria-labelledby="s-exec">
        <h2 id="s-exec" className="font-semibold">4. Execution</h2>
        <div className="grid gap-3 sm:grid-cols-3">
          <Num label="Cooldown (seconds)" value={exec.cooldown_ms / 1000} min={0} max={86400} hint="Ignore new triggers for this long" onChange={(v) => onChange({ ...doc, execution: { ...exec, cooldown_ms: Math.round(v * 1000) } })} />
          <Num label="Max runs per hour" value={exec.max_executions_per_hour} min={1} max={3600} onChange={(v) => onChange({ ...doc, execution: { ...exec, max_executions_per_hour: v } })} />
          <Field label="If still running">
            <label className="flex min-h-10 items-center gap-2 text-sm">
              <Switch checked={exec.allow_parallel} onCheckedChange={(v) => onChange({ ...doc, execution: { ...exec, allow_parallel: v } })} aria-label="Allow parallel runs" />
              {exec.allow_parallel ? "Start another run" : "Skip the new trigger"}
            </label>
          </Field>
        </div>
      </section>
    </div>
  );
}
