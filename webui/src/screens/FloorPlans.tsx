// Floor plans (DESIGN §7.4, §9): list, upload (sanitised client-side, then
// checked again by the controller), Control mode (inline SVG, live glows) and
// Edit mode (Konva, lazy). Deep links open Control mode.
import * as React from "react";
import { Link, Route, Routes, useNavigate, useParams } from "react-router-dom";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { FileUp, Map as MapIcon, Pencil, Plus, Trash2 } from "lucide-react";
import { toast } from "sonner";

import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Skeleton } from "@/components/ui/skeleton";
import { ConfirmDialog, EmptyState, Field, PageHeader } from "@/components/common";
import { errorMessage } from "@/hooks/useAuth";
import { useLights, useRooms } from "@/services/queries";
import { cn } from "@/lib/utils";
import { floorplanApi, floorplanKeys, type FloorPlanT } from "./floorplans/api";
import { sanitizeSvg, SvgRejected } from "./floorplans/sanitizeSvg";
import { isDirty, useEditorStore } from "./floorplans/editorStore";

const FloorPlanViewer = React.lazy(() => import("./floorplans/FloorPlanViewer"));
const FloorPlanEditor = React.lazy(() => import("./floorplans/FloorPlanEditor"));

export default function FloorPlans() {
  return (
    <Routes>
      <Route index element={<PlanList />} />
      <Route path=":id" element={<PlanPage mode="control" />} />
      <Route path=":id/edit" element={<PlanPage mode="edit" />} />
    </Routes>
  );
}

function PlanList() {
  const q = useQuery({ queryKey: floorplanKeys.list, queryFn: floorplanApi.list });
  const qc = useQueryClient();
  const navigate = useNavigate();
  const [name, setName] = React.useState("");
  const create = async () => {
    try {
      const p = await floorplanApi.create(name.trim());
      await qc.invalidateQueries({ queryKey: floorplanKeys.list });
      navigate(`/floorplans/${p.id}/edit`);
    } catch (e) {
      toast.error("Could not create the floor plan", { description: errorMessage(e) });
    }
  };
  return (
    <div className="space-y-6">
      <PageHeader title="Floor plans" description="Upload a drawing of your home, place the lights on it, then control them from the plan." />
      {q.isLoading ? (
        <Skeleton className="h-24" />
      ) : q.data && q.data.length > 0 ? (
        <ul className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
          {q.data.map((p) => (
            <li key={p.id}>
              <Link to={`/floorplans/${p.id}`} className="block rounded-xl border bg-card p-4 hover:border-primary">
                <p className="font-medium">{p.name}</p>
                <p className="text-sm text-muted-foreground">
                  {p.hasSvg ? `${p.lights} light${p.lights === 1 ? "" : "s"} · ${p.rooms} room${p.rooms === 1 ? "" : "s"}` : "No drawing yet"}
                </p>
              </Link>
            </li>
          ))}
        </ul>
      ) : (
        <EmptyState icon={MapIcon} title="No floor plans yet" description="Create one, then upload an SVG drawing (exported from a floor-plan or drawing app)." />
      )}
      {(q.data?.length ?? 0) < 3 && (
        <form className="flex flex-wrap items-end gap-2" onSubmit={(e) => { e.preventDefault(); if (name.trim()) void create(); }}>
          <div className="w-64">
            <Field label="New floor plan" htmlFor="fp-name">
              <Input id="fp-name" value={name} onChange={(e) => setName(e.target.value)} maxLength={48} placeholder="Ground floor" />
            </Field>
          </div>
          <Button type="submit" disabled={!name.trim()}><Plus className="size-4" /> Create</Button>
        </form>
      )}
    </div>
  );
}

function PlanPage({ mode }: { mode: "control" | "edit" }) {
  const { id = "" } = useParams();
  const navigate = useNavigate();
  const qc = useQueryClient();
  const plan = useQuery({ queryKey: floorplanKeys.plan(id), queryFn: () => floorplanApi.get(id) });
  const svg = useQuery({ queryKey: floorplanKeys.svg(id), queryFn: () => floorplanApi.svg(id), enabled: plan.data?.hasSvg === true, staleTime: Infinity });
  const lights = useLights();
  const rooms = useRooms();
  const editor = useEditorStore();
  const dirty = mode === "edit" && isDirty(editor);
  const [confirmLeave, setConfirmLeave] = React.useState<string | null>(null);
  const [confirmDelete, setConfirmDelete] = React.useState(false);
  const [saving, setSaving] = React.useState(false);

  // Load the saved layout into the editor draft (once per plan version).
  const loadedFor = React.useRef<string>("");
  React.useEffect(() => {
    if (mode !== "edit" || !plan.data) return;
    const key = `${plan.data.id}:${plan.dataUpdatedAt}`;
    if (loadedFor.current === key || (isDirty(useEditorStore.getState()) && useEditorStore.getState().planId === plan.data.id)) return;
    loadedFor.current = key;
    useEditorStore.getState().load(plan.data.id, { lights: plan.data.lights, rooms: plan.data.rooms });
  }, [mode, plan.data, plan.dataUpdatedAt]);

  React.useEffect(() => {
    if (!dirty) return;
    const warn = (e: BeforeUnloadEvent) => e.preventDefault();
    window.addEventListener("beforeunload", warn);
    return () => window.removeEventListener("beforeunload", warn);
  }, [dirty]);

  const go = (to: string) => (dirty ? setConfirmLeave(to) : navigate(to));

  if (plan.isError) return <PageHeader title="Floor plan" description={errorMessage(plan.error)} />;
  if (!plan.data || !lights.data || !rooms.data) return <Skeleton className="h-96" />;
  const p = plan.data;
  const knownLights = new Set(lights.data.map((l) => l.id));
  const knownRooms = new Set(rooms.data.map((r) => r.id));

  const save = async () => {
    setSaving(true);
    try {
      const d = useEditorStore.getState().draft;
      // Drop references to lights/rooms deleted since the plan was edited.
      const layout = { lights: d.lights.filter((l) => knownLights.has(l.lightId)), rooms: d.rooms.filter((r) => knownRooms.has(r.roomId)) };
      const saved = await floorplanApi.saveLayout(p.id, layout);
      qc.setQueryData(floorplanKeys.plan(p.id), saved);
      useEditorStore.getState().markSaved({ lights: saved.lights, rooms: saved.rooms });
      void qc.invalidateQueries({ queryKey: floorplanKeys.list });
      toast.success("Floor plan saved", { duration: 2000 });
    } catch (e) {
      toast.error("Not saved", { description: errorMessage(e) });
    } finally {
      setSaving(false);
    }
  };

  return (
    <div className={cn("space-y-4", mode === "edit" && "rounded-2xl border-2 border-primary/60 bg-primary/5 p-3")}>
      <PageHeader
        title={p.name}
        description={mode === "edit" ? "Editing — changes apply when you save." : undefined}
        actions={
          <>
            {mode === "edit" && <Badge className="gap-1"><Pencil className="size-3" aria-hidden="true" /> Editing</Badge>}
            <div className="inline-flex rounded-lg border p-0.5" role="radiogroup" aria-label="Mode">
              {(["control", "edit"] as const).map((m) => (
                <button
                  key={m}
                  type="button"
                  role="radio"
                  aria-checked={mode === m}
                  onClick={() => m !== mode && go(m === "edit" ? `/floorplans/${p.id}/edit` : `/floorplans/${p.id}`)}
                  className={cn("min-h-11 rounded-md px-4 text-sm font-medium", mode === m ? "bg-primary text-primary-foreground" : "text-muted-foreground hover:text-foreground")}
                >
                  {m === "control" ? "Control" : "Edit"}
                </button>
              ))}
            </div>
          </>
        }
      />
      {mode === "edit" && (
        <div className="flex flex-wrap items-center gap-2">
          <Button onClick={() => void save()} disabled={!dirty || saving}>Save</Button>
          <Button variant="outline" onClick={() => useEditorStore.getState().load(p.id, { lights: p.lights, rooms: p.rooms })} disabled={!dirty}>Discard changes</Button>
          <UploadButton plan={p} />
          <Button variant="ghost" className="text-destructive" onClick={() => setConfirmDelete(true)}><Trash2 className="size-4" /> Delete plan</Button>
          {dirty && <span className="text-sm text-muted-foreground">Unsaved changes</span>}
        </div>
      )}
      {!p.hasSvg ? (
        <EmptyState icon={FileUp} title="Upload a drawing" description="An SVG of this floor (max 128 KB). Scripts, external links and unsafe content are removed before it is stored." action={<UploadButton plan={p} />} />
      ) : svg.isError ? (
        <p className="text-sm text-destructive">{errorMessage(svg.error)}</p>
      ) : !svg.data ? (
        <Skeleton className="h-96" />
      ) : (
        <React.Suspense fallback={<Skeleton className="h-96" />}>
          {mode === "control" ? (
            <FloorPlanViewer plan={p} svg={svg.data} lights={lights.data} rooms={rooms.data} />
          ) : (
            <FloorPlanEditor plan={p} svg={svg.data} lights={lights.data} rooms={rooms.data} />
          )}
        </React.Suspense>
      )}
      <ConfirmDialog
        open={confirmLeave !== null}
        onOpenChange={(o) => !o && setConfirmLeave(null)}
        title="Leave without saving?"
        description="Your changes to this floor plan will be lost."
        confirmLabel="Discard changes"
        destructive
        onConfirm={() => {
          useEditorStore.getState().load(p.id, { lights: p.lights, rooms: p.rooms });
          if (confirmLeave) navigate(confirmLeave);
        }}
      />
      <ConfirmDialog
        open={confirmDelete}
        onOpenChange={setConfirmDelete}
        title={`Delete “${p.name}”?`}
        description="The drawing and all light positions on it are removed. Lights themselves are not affected."
        confirmLabel="Delete"
        destructive
        onConfirm={async () => {
          try {
            await floorplanApi.remove(p.id);
            await qc.invalidateQueries({ queryKey: floorplanKeys.list });
            useEditorStore.getState().load("", { lights: [], rooms: [] });
            navigate("/floorplans");
          } catch (e) {
            toast.error("Could not delete", { description: errorMessage(e) });
          }
        }}
      />
    </div>
  );
}

function UploadButton({ plan }: { plan: FloorPlanT }) {
  const qc = useQueryClient();
  const input = React.useRef<HTMLInputElement>(null);
  const [busy, setBusy] = React.useState(false);
  const onFile = async (file: File | undefined) => {
    if (!file) return;
    setBusy(true);
    try {
      const clean = sanitizeSvg(await file.text());
      const saved = await floorplanApi.uploadSvg(plan.id, clean.svg);
      qc.setQueryData(floorplanKeys.plan(plan.id), saved);
      qc.setQueryData(floorplanKeys.svg(plan.id), clean.svg);
      useEditorStore.getState().load(plan.id, { lights: saved.lights, rooms: saved.rooms });
      void qc.invalidateQueries({ queryKey: floorplanKeys.list });
      toast.success("Drawing uploaded", {
        description: [
          clean.removed.length ? `Removed for safety: ${clean.removed.slice(0, 4).join(", ")}${clean.removed.length > 4 ? "…" : ""}.` : "",
          saved.layoutReset ? "The drawing size changed, so light positions were cleared." : "",
        ].filter(Boolean).join(" ") || undefined,
        duration: 6000,
      });
    } catch (e) {
      toast.error("Drawing not uploaded", { description: e instanceof SvgRejected ? e.message : errorMessage(e), duration: 8000 });
    } finally {
      setBusy(false);
      if (input.current) input.current.value = "";
    }
  };
  return (
    <>
      <input ref={input} type="file" accept=".svg,image/svg+xml" className="sr-only" aria-label="SVG drawing" onChange={(e) => void onFile(e.target.files?.[0])} />
      <Button variant="outline" onClick={() => input.current?.click()} disabled={busy}>
        <FileUp className="size-4" /> {plan.hasSvg ? "Replace drawing" : "Upload SVG"}
      </Button>
    </>
  );
}
