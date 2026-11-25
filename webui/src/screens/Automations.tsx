// Automations (DESIGN §7.6): list with enable switches, last result, Test,
// history; editor with Simple (form) and Advanced (React Flow, lazy) tabs;
// location/clock settings for sunrise/sunset triggers.
import * as React from "react";
import { Link, Route, Routes, useNavigate, useParams } from "react-router-dom";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { History, LocateFixed, Pencil, Play, Plus, Trash2, Workflow } from "lucide-react";
import { toast } from "sonner";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Switch } from "@/components/ui/switch";
import { Badge } from "@/components/ui/badge";
import { Skeleton } from "@/components/ui/skeleton";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { ConfirmDialog, EmptyState, Field, PageHeader, StatusDot } from "@/components/common";
import { automationApi } from "./automations/api";
import { useEffects, useGroups, useHardwareStatus, useLights, useRooms, useScenes } from "@/services/queries";
import { errorMessage } from "@/hooks/useAuth";
import { SimpleEditor, type Refs } from "./automations/SimpleEditor";
import type { FlowEditorHandle } from "./automations/FlowEditor";
import { describe as describeDoc, fromGraph, newAutomation, slugify, type AutomationDocT, type ConditionT } from "./automations/model";

const FlowEditor = React.lazy(() => import("./automations/FlowEditor"));

const keys = {
  list: ["automations"] as const,
  doc: (id: string) => ["automations", id] as const,
  history: (id?: string) => ["automations", "history", id ?? "all"] as const,
  settings: ["automations", "settings"] as const,
};

function useRefs(): Refs {
  const lights = useLights();
  const scenes = useScenes();
  const groups = useGroups();
  const rooms = useRooms();
  const hw = useHardwareStatus();
  const effects = useEffects();
  const list = useQuery({ queryKey: keys.list, queryFn: automationApi.list });
  return {
    lights: (lights.data ?? []).map((l) => ({ id: l.id, name: l.name })),
    inputs: (hw.data?.inputs ?? []).map((i) => ({ id: i.id, name: i.name })),
    outputs: (hw.data?.outputs ?? []).filter((o) => !o.lightId).map((o) => ({ id: o.id, name: o.name })),
    scenes: (scenes.data ?? []).map((s) => ({ id: s.id, name: s.name })),
    groups: (groups.data ?? []).map((g) => ({ id: g.id, name: g.name })),
    rooms: (rooms.data ?? []).map((r) => ({ id: r.id, name: r.name })),
    automations: (list.data ?? []).map((a) => ({ id: a.id, name: a.name })),
    effects: effects.data ?? [],
  };
}

function resultBadge(r: string) {
  if (!r) return <Badge variant="secondary">Never run</Badge>;
  if (r === "ok") return <Badge variant="secondary">OK</Badge>;
  return <Badge variant="destructive">{r.replace("_", " ")}</Badge>;
}

function HistoryDialog({ id, open, onOpenChange }: { id?: string; open: boolean; onOpenChange: (o: boolean) => void }) {
  const h = useQuery({ queryKey: keys.history(id), queryFn: () => automationApi.history(id), enabled: open, refetchInterval: open ? 3000 : false });
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-h-[85dvh] overflow-y-auto sm:max-w-lg">
        <DialogHeader>
          <DialogTitle>Execution history</DialogTitle>
          <DialogDescription>Most recent first (kept in memory, last 32 runs).</DialogDescription>
        </DialogHeader>
        {(h.data ?? []).length === 0 ? (
          <p className="text-sm text-muted-foreground">Nothing has run yet.</p>
        ) : (
          <ul className="divide-y text-sm">
            {(h.data ?? []).map((e, i) => (
              <li key={i} className="space-y-0.5 py-2">
                <div className="flex items-center gap-2">
                  {resultBadge(e.result)}
                  <span className="font-medium">{e.automationId}</span>
                  <span className="ml-auto text-xs text-muted-foreground">
                    {e.unixTime ? new Date(e.unixTime * 1000).toLocaleString() : `uptime ${Math.round(e.uptimeMs / 1000)} s`}
                  </span>
                </div>
                <p className="text-xs text-muted-foreground">Trigger: {e.trigger} · {e.durationMs} ms</p>
                {e.message && <p className="text-xs text-destructive">{e.message}</p>}
              </li>
            ))}
          </ul>
        )}
      </DialogContent>
    </Dialog>
  );
}

function LocationCard() {
  const qc = useQueryClient();
  const settings = useQuery({ queryKey: keys.settings, queryFn: automationApi.settings, refetchInterval: 30000 });
  const [lat, setLat] = React.useState("");
  const [lon, setLon] = React.useState("");
  const s = settings.data;
  const save = useMutation({
    mutationFn: () => automationApi.setLocation(Number(lat), Number(lon)),
    onSuccess: () => {
      toast.success("Location saved", { duration: 2500 });
      void qc.invalidateQueries({ queryKey: keys.settings });
    },
    onError: (e) => toast.error("Could not save", { description: errorMessage(e) }),
  });
  if (!s) return null;
  return (
    <section className="space-y-3 rounded-xl border bg-card p-4" aria-labelledby="loc-h">
      <div className="flex flex-wrap items-center gap-3">
        <h2 id="loc-h" className="mr-auto font-semibold">Clock and location</h2>
        <StatusDot status={s.timeSynced ? "ok" : "warn"} label={s.timeSynced ? `Time ${s.localTime}` : "Clock not synchronised — schedules wait"} />
      </div>
      <p className="text-sm text-muted-foreground">
        {s.hasLocation
          ? `Sunrise ${s.sunrise ?? "—"}, sunset ${s.sunset ?? "—"} today at ${s.latitude.toFixed(3)}, ${s.longitude.toFixed(3)}. Times use the controller's time zone (Network settings).`
          : "Set the location to use sunrise and sunset triggers. It is computed on the controller, no internet service is used."}
      </p>
      <div className="flex flex-wrap items-end gap-2">
        <Field label="Latitude" htmlFor="lat"><Input id="lat" className="w-32" inputMode="decimal" placeholder={String(s.latitude)} value={lat} onChange={(e) => setLat(e.target.value)} /></Field>
        <Field label="Longitude" htmlFor="lon"><Input id="lon" className="w-32" inputMode="decimal" placeholder={String(s.longitude)} value={lon} onChange={(e) => setLon(e.target.value)} /></Field>
        <Button variant="outline" onClick={() => navigator.geolocation?.getCurrentPosition((p) => { setLat(p.coords.latitude.toFixed(4)); setLon(p.coords.longitude.toFixed(4)); }, () => toast.error("Browser location unavailable"))}>
          <LocateFixed className="size-4" /> Use this device's location
        </Button>
        <Button disabled={!lat || !lon || save.isPending} onClick={() => save.mutate()}>Save</Button>
      </div>
    </section>
  );
}

function AutomationList() {
  const qc = useQueryClient();
  const list = useQuery({ queryKey: keys.list, queryFn: automationApi.list, refetchInterval: 5000 });
  const [historyFor, setHistoryFor] = React.useState<string | undefined | null>(null);
  const [deleting, setDeleting] = React.useState<string | null>(null);
  const toggle = useMutation({
    mutationFn: ({ id, enabled }: { id: string; enabled: boolean }) => automationApi.enable(id, enabled),
    onSuccess: () => void qc.invalidateQueries({ queryKey: keys.list }),
    onError: (e) => toast.error("Could not change", { description: errorMessage(e), duration: Infinity }),
  });
  const test = useMutation({
    mutationFn: (id: string) => automationApi.test(id),
    onSuccess: (_d, id) => {
      toast.success(`“${id}” started`, { duration: 2500 });
      setTimeout(() => void qc.invalidateQueries({ queryKey: keys.list }), 800);
    },
    onError: (e) => toast.error("Test failed", { description: errorMessage(e), duration: Infinity }),
  });
  const del = useMutation({
    mutationFn: (id: string) => automationApi.remove(id),
    onSuccess: () => void qc.invalidateQueries({ queryKey: keys.list }),
    onError: (e) => toast.error("Could not delete", { description: errorMessage(e), duration: Infinity }),
  });
  return (
    <div className="space-y-6">
      <PageHeader
        title="Automations"
        description="Run locally on the controller — no internet or Home Assistant needed."
        actions={
          <>
            <Button variant="outline" onClick={() => setHistoryFor(undefined)}><History className="size-4" /> History</Button>
            <Button asChild><Link to="/automations/new"><Plus className="size-4" /> New automation</Link></Button>
          </>
        }
      />
      <LocationCard />
      {list.isLoading ? (
        <Skeleton className="h-24 rounded-xl" />
      ) : (list.data ?? []).length === 0 ? (
        <EmptyState icon={Workflow} title="No automations yet" description="React to buttons, schedules, sunrise/sunset or light changes." action={<Button asChild><Link to="/automations/new">New automation</Link></Button>} />
      ) : (
        <div className="divide-y rounded-xl border bg-card">
          {(list.data ?? []).map((a) => (
            <div key={a.id} className="flex flex-wrap items-center gap-3 p-4 sm:flex-nowrap">
              <Switch checked={a.enabled} aria-label={`${a.name} enabled`} onCheckedChange={(enabled) => toggle.mutate({ id: a.id, enabled })} />
              <div className="min-w-0 flex-1">
                <p className="flex items-center gap-2 font-medium">
                  {a.name} {a.runtime.running && <Badge>Running</Badge>}
                </p>
                <p className="text-sm text-muted-foreground">{a.summary}</p>
              </div>
              {resultBadge(a.runtime.lastResult)}
              <span className="text-xs text-muted-foreground">{a.runtime.runs} runs</span>
              <Button variant="ghost" size="icon" aria-label={`Test ${a.name}`} onClick={() => test.mutate(a.id)}><Play className="size-4" /></Button>
              <Button variant="ghost" size="icon" aria-label={`History of ${a.name}`} onClick={() => setHistoryFor(a.id)}><History className="size-4" /></Button>
              <Button variant="ghost" size="icon" asChild aria-label={`Edit ${a.name}`}><Link to={`/automations/${a.id}`}><Pencil className="size-4" /></Link></Button>
              <Button variant="ghost" size="icon" aria-label={`Delete ${a.name}`} onClick={() => setDeleting(a.id)}><Trash2 className="size-4" /></Button>
            </div>
          ))}
        </div>
      )}
      <HistoryDialog id={historyFor ?? undefined} open={historyFor !== null} onOpenChange={(o) => !o && setHistoryFor(null)} />
      <ConfirmDialog open={deleting !== null} onOpenChange={(o) => !o && setDeleting(null)} destructive title="Delete automation?" description="It stops immediately and cannot be recovered." confirmLabel="Delete" onConfirm={() => deleting && del.mutate(deleting)} />
    </div>
  );
}

function hasNested(doc: AutomationDocT) {
  return (doc.conditions?.items ?? []).some((c: ConditionT) => c.type === "and" || c.type === "or" || c.type === "not");
}

function EditorPage() {
  const { id } = useParams();
  const isNew = id === "new";
  const existing = useQuery({ queryKey: keys.doc(id ?? ""), queryFn: () => automationApi.get(id ?? ""), enabled: !isNew });
  if (!isNew && existing.isLoading) return <Skeleton className="h-64 rounded-xl" />;
  if (!isNew && !existing.data) return <EmptyState icon={Workflow} title="Automation not found" description={errorMessage(existing.error)} />;
  return <Editor key={id} initial={isNew ? newAutomation() : existing.data!} isNew={isNew} />;
}

function Editor({ initial, isNew }: { initial: AutomationDocT; isNew: boolean }) {
  const navigate = useNavigate();
  const qc = useQueryClient();
  const refs = useRefs();
  const [doc, setDoc] = React.useState<AutomationDocT>(initial);
  const [tab, setTab] = React.useState<string>(initial.mode === "advanced" || hasNested(initial) ? "advanced" : "simple");
  const [flowKey, setFlowKey] = React.useState(0);
  const [errors, setErrors] = React.useState<string[]>([]);
  const flow = React.useRef<FlowEditorHandle>(null);
  const names = (kind: string, target?: string) => {
    if (!target) return "?";
    const list = kind === "input" ? refs.inputs : kind === "scene" ? refs.scenes : kind === "group" ? refs.groups : kind === "room" ? refs.rooms : refs.lights;
    return list.find((x) => x.id === target)?.name ?? target;
  };

  // Pull the graph into the document (advanced tab).
  const fromFlow = (): AutomationDocT | null => {
    const g = flow.current?.graph();
    if (!g) return doc;
    const r = fromGraph(g.nodes, g.edges);
    if (r.errors.length || !r.trigger) {
      setErrors(r.errors);
      return null;
    }
    setErrors([]);
    return { ...doc, trigger: r.trigger, conditions: r.conditions, actions: r.actions, graph: r.graph, mode: "advanced" };
  };

  const switchTab = (next: string) => {
    if (next === tab) return;
    if (tab === "advanced") {
      const d = fromFlow();
      if (!d) return;
      if (next === "simple" && hasNested(d)) {
        setErrors(["Nested AND/OR conditions can only be edited in the Advanced editor."]);
        return;
      }
      setDoc(d);
    } else {
      setFlowKey((k) => k + 1);
    }
    setTab(next);
  };

  const save = useMutation({
    mutationFn: async () => {
      const d = tab === "advanced" ? fromFlow() : { ...doc, mode: "simple" as const, graph: undefined };
      if (!d) throw new Error("Fix the graph first");
      if (!d.name.trim()) throw new Error("Give the automation a name");
      const body = { ...d, id: isNew ? slugify(d.name) : d.id, summary: undefined, runtime: undefined };
      return isNew ? automationApi.create(body) : automationApi.update(d.id, body);
    },
    onSuccess: (s) => {
      toast.success("Automation saved", { duration: 2500 });
      void qc.invalidateQueries({ queryKey: ["automations"] });
      navigate(isNew ? `/automations/${s.id}` : "/automations");
    },
    onError: (e) => toast.error("Could not save", { description: errorMessage(e), duration: Infinity }),
  });

  return (
    <div className="space-y-6">
      <Link to="/automations" className="text-sm text-muted-foreground hover:text-foreground">← Automations</Link>
      <PageHeader
        title={isNew ? "New automation" : `Edit “${initial.name}”`}
        actions={
          <>
            {!isNew && <Button variant="outline" onClick={() => automationApi.test(initial.id).then(() => toast.success("Test started", { duration: 2000 })).catch((e) => toast.error("Test failed", { description: errorMessage(e) }))}><Play className="size-4" /> Test</Button>}
            <Button onClick={() => save.mutate()} disabled={save.isPending}>Save</Button>
          </>
        }
      />
      <div className="grid gap-4 sm:grid-cols-[1fr_auto]">
        <Field label="Name" htmlFor="auto-name">
          <Input id="auto-name" value={doc.name} maxLength={80} onChange={(e) => setDoc({ ...doc, name: e.target.value })} placeholder="Evening kitchen lights" />
        </Field>
        <Field label="Enabled">
          <div className="flex min-h-10 items-center"><Switch checked={doc.enabled} onCheckedChange={(v) => setDoc({ ...doc, enabled: v })} aria-label="Enabled" /></div>
        </Field>
      </div>
      {tab === "simple" && <p className="rounded-lg bg-muted px-3 py-2 text-sm">{describeDoc(doc, names)}</p>}
      {errors.length > 0 && (
        <ul className="rounded-lg border border-destructive/50 bg-destructive/10 px-4 py-2 text-sm text-destructive" role="alert">
          {errors.map((e) => <li key={e}>{e}</li>)}
        </ul>
      )}
      <Tabs value={tab} onValueChange={switchTab}>
        <TabsList>
          <TabsTrigger value="simple">Simple</TabsTrigger>
          <TabsTrigger value="advanced">Advanced</TabsTrigger>
        </TabsList>
        <TabsContent value="simple" className="mt-4">
          {hasNested(doc) ? (
            <p className="text-sm text-muted-foreground">This automation uses nested condition groups and can only be edited in the Advanced editor.</p>
          ) : (
            <SimpleEditor doc={doc} onChange={setDoc} refs={refs} />
          )}
        </TabsContent>
        <TabsContent value="advanced" className="mt-4">
          <React.Suspense fallback={<Skeleton className="h-[28rem] rounded-xl" />}>
            {tab === "advanced" && <FlowEditor key={flowKey} ref={flow} doc={doc} refs={refs} />}
          </React.Suspense>
        </TabsContent>
      </Tabs>
    </div>
  );
}

export default function Automations() {
  return (
    <Routes>
      <Route index element={<AutomationList />} />
      <Route path=":id" element={<EditorPage />} />
    </Routes>
  );
}
