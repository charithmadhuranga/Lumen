// Scenes (DESIGN §7.4): card grid with Activate; "Save current state as
// scene" is the primary action; editor with per-light state, transition and
// live preview; JSON export/import validated with Zod.
import * as React from "react";
import { Copy, Download, MoreVertical, Pencil, Play, Plus, Sparkles, Trash2, Upload } from "lucide-react";
import { toast } from "sonner";
import { z } from "zod";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Slider } from "@/components/ui/slider";
import { Switch } from "@/components/ui/switch";
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { DropdownMenu, DropdownMenuContent, DropdownMenuItem, DropdownMenuTrigger } from "@/components/ui/dropdown-menu";
import { Skeleton } from "@/components/ui/skeleton";
import { ConfirmDialog, EmptyState, Field, PageHeader } from "@/components/common";
import {
  useActivateScene,
  useCaptureScene,
  useCreateScene,
  useDeleteScene,
  useDuplicateScene,
  useLightCommand,
  useLights,
  useScenes,
  useUpdateScene,
} from "@/services/queries";
import { Scene, type Light, type SceneLightState } from "@/schemas";
import { hexToRgb, rgbToHex } from "@/lib/color";

function sceneTint(scene: Scene): string {
  const coloured = scene.states.filter((s) => s.power && (s.red || s.green || s.blue));
  if (coloured.length) {
    const c = coloured[0];
    return `linear-gradient(135deg, rgb(${c.red} ${c.green} ${c.blue} / 0.35), transparent)`;
  }
  const on = scene.states.filter((s) => s.power).length;
  return on ? "linear-gradient(135deg, color-mix(in oklch, var(--light-on-glow) 35%, transparent), transparent)" : "none";
}

function download(name: string, data: unknown) {
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  URL.revokeObjectURL(url);
}

function CaptureDialog({ open, onOpenChange, lights }: { open: boolean; onOpenChange: (o: boolean) => void; lights: Light[] }) {
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      {open && <CaptureForm lights={lights} onOpenChange={onOpenChange} />}
    </Dialog>
  );
}

function CaptureForm({ onOpenChange, lights }: { onOpenChange: (o: boolean) => void; lights: Light[] }) {
  const capture = useCaptureScene();
  const [name, setName] = React.useState("");
  const [ids, setIds] = React.useState<string[]>(() => lights.map((l) => l.id));
  return (
    <>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Save current state as scene</DialogTitle>
          <DialogDescription>Captures what the selected lights are doing right now.</DialogDescription>
        </DialogHeader>
        <Field label="Scene name" htmlFor="scene-name">
          <Input id="scene-name" value={name} onChange={(e) => setName(e.target.value)} placeholder="Movie night" autoFocus maxLength={64} />
        </Field>
        <fieldset className="max-h-56 space-y-1 overflow-y-auto">
          <legend className="mb-1 text-sm font-medium">Include lights</legend>
          {lights.map((l) => (
            <label key={l.id} className="flex min-h-11 items-center gap-3 rounded-md px-2 hover:bg-muted">
              <input
                type="checkbox"
                className="size-4 accent-[var(--primary)]"
                checked={ids.includes(l.id)}
                onChange={(e) => setIds(e.target.checked ? [...ids, l.id] : ids.filter((x) => x !== l.id))}
              />
              <span className="flex-1 text-sm">{l.name}</span>
              <span className="text-xs text-muted-foreground">{l.state.power ? `${l.state.brightness}%` : "off"}</span>
            </label>
          ))}
        </fieldset>
        <DialogFooter>
          <Button variant="outline" onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button
            disabled={!name.trim() || ids.length === 0 || capture.isPending}
            onClick={() => capture.mutate({ name: name.trim(), lightIds: ids }, { onSuccess: () => onOpenChange(false) })}
          >
            Save scene
          </Button>
        </DialogFooter>
      </DialogContent>
    </>
  );
}

function StateRow({ light, value, onChange }: { light: Light; value: SceneLightState; onChange: (v: SceneLightState) => void }) {
  const colour = ["rgb", "rgbw", "addressable_rgb", "addressable_rgbw"].includes(light.type);
  return (
    <div className="space-y-2 rounded-lg border p-3">
      <div className="flex items-center gap-3">
        <Switch checked={value.power} onCheckedChange={(power) => onChange({ ...value, power })} aria-label={`${light.name} on in this scene`} />
        <span className="flex-1 text-sm font-medium">{light.name}</span>
        {colour && (
          <input
            type="color"
            aria-label={`${light.name} colour`}
            className="h-8 w-10 cursor-pointer rounded border bg-transparent"
            value={rgbToHex({ r: value.red, g: value.green, b: value.blue })}
            onChange={(e) => {
              const c = hexToRgb(e.target.value);
              if (c) onChange({ ...value, red: c.r, green: c.g, blue: c.b });
            }}
          />
        )}
      </div>
      {light.type !== "switch" && value.power && (
        <div className="flex items-center gap-3">
          <Slider value={[value.brightness]} min={1} max={100} step={1} className="py-3" aria-label={`${light.name} brightness`}
            onValueChange={([b]) => onChange({ ...value, brightness: b })} />
          <span className="w-10 text-right text-xs tabular-nums text-muted-foreground">{value.brightness}%</span>
        </div>
      )}
      {light.type === "cct" && value.power && (
        <div className="flex items-center gap-3">
          <Slider value={[value.colorTemperature || light.limits.colorTemperatureMin]} min={light.limits.colorTemperatureMin} max={light.limits.colorTemperatureMax} step={50} className="py-3" aria-label={`${light.name} colour temperature`}
            onValueChange={([k]) => onChange({ ...value, colorTemperature: k })} />
          <span className="w-14 text-right text-xs tabular-nums text-muted-foreground">{value.colorTemperature} K</span>
        </div>
      )}
    </div>
  );
}

function SceneEditor({ scene, lights, open, onOpenChange }: { scene: Scene | null; lights: Light[]; open: boolean; onOpenChange: (o: boolean) => void }) {
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      {open && <SceneEditorForm key={scene?.id ?? "new"} scene={scene} lights={lights} onOpenChange={onOpenChange} />}
    </Dialog>
  );
}

function SceneEditorForm({ scene, lights, onOpenChange }: { scene: Scene | null; lights: Light[]; onOpenChange: (o: boolean) => void }) {
  const update = useUpdateScene();
  const create = useCreateScene();
  const command = useLightCommand();
  const [name, setName] = React.useState(scene?.name ?? "");
  const [transition, setTransition] = React.useState(scene?.transitionMs ?? 500);
  const [states, setStates] = React.useState<SceneLightState[]>(
    () =>
      scene?.states ??
      lights.map((l) => ({ lightId: l.id, power: l.state.power, brightness: l.state.brightness || 100, red: l.state.red, green: l.state.green, blue: l.state.blue, white: l.state.white, colorTemperature: l.state.colorTemperature })),
  );
  const [snapshot] = React.useState<Light[]>(lights);

  const included = new Set(states.map((s) => s.lightId));
  const preview = () =>
    states.forEach((s) =>
      command.mutate({ id: s.lightId, command: { power: s.power, ...(s.power ? { brightness: s.brightness } : {}), ...(s.red || s.green || s.blue ? { red: s.red, green: s.green, blue: s.blue } : {}), transitionMs: 300 } }),
    );
  const revert = () =>
    snapshot.forEach((l) => command.mutate({ id: l.id, command: { power: l.state.power, ...(l.type !== "switch" && l.state.power ? { brightness: l.state.brightness } : {}), transitionMs: 300 } }));

  const save = async () => {
    const body = { name: name.trim(), transitionMs: transition, states };
    if (scene) await update.mutateAsync({ id: scene.id, scene: body });
    else await create.mutateAsync(body);
    onOpenChange(false);
  };

  return (
    <>
      <DialogContent className="max-h-[90dvh] overflow-y-auto sm:max-w-xl">
        <DialogHeader>
          <DialogTitle>{scene ? `Edit “${scene.name}”` : "New scene"}</DialogTitle>
          <DialogDescription>Set each light, preview it live, then save.</DialogDescription>
        </DialogHeader>
        <Field label="Name" htmlFor="scene-edit-name">
          <Input id="scene-edit-name" value={name} onChange={(e) => setName(e.target.value)} maxLength={64} />
        </Field>
        <Field label={`Transition: ${(transition / 1000).toFixed(1)} s`}>
          <Slider value={[transition]} min={0} max={10000} step={100} className="py-3" aria-label="Transition duration" onValueChange={([t]) => setTransition(t)} />
        </Field>
        <div className="space-y-2">
          {states.map((st, i) => {
            const light = lights.find((l) => l.id === st.lightId);
            if (!light) return null;
            return (
              <div key={st.lightId} className="flex items-start gap-2">
                <div className="flex-1">
                  <StateRow light={light} value={st} onChange={(v) => setStates(states.map((x, j) => (j === i ? v : x)))} />
                </div>
                <Button variant="ghost" size="icon" aria-label={`Remove ${light.name} from scene`} onClick={() => setStates(states.filter((_, j) => j !== i))}>
                  <Trash2 className="size-4" />
                </Button>
              </div>
            );
          })}
          {lights.filter((l) => !included.has(l.id)).length > 0 && (
            <div className="flex flex-wrap gap-2 pt-1">
              {lights.filter((l) => !included.has(l.id)).map((l) => (
                <Button key={l.id} size="sm" variant="outline" onClick={() => setStates([...states, { lightId: l.id, power: true, brightness: 100, red: 255, green: 255, blue: 255, white: 0, colorTemperature: l.limits.colorTemperatureMin }])}>
                  <Plus className="size-3" /> {l.name}
                </Button>
              ))}
            </div>
          )}
        </div>
        <DialogFooter className="flex-wrap gap-2">
          <Button variant="outline" onClick={preview}><Play className="size-4" /> Preview</Button>
          <Button variant="outline" onClick={revert}>Revert</Button>
          <Button onClick={() => void save()} disabled={!name.trim() || states.length === 0 || update.isPending || create.isPending}>Save scene</Button>
        </DialogFooter>
      </DialogContent>
    </>
  );
}

const SceneImport = z.object({ name: z.string().min(1).max(64), transitionMs: z.number().int().min(0).max(600000), states: Scene.shape.states });

export default function Scenes() {
  const scenes = useScenes();
  const lights = useLights();
  const activate = useActivateScene();
  const duplicate = useDuplicateScene();
  const del = useDeleteScene();
  const create = useCreateScene();
  const [capturing, setCapturing] = React.useState(false);
  const [editing, setEditing] = React.useState<Scene | null | undefined>(undefined);
  const [deleting, setDeleting] = React.useState<Scene | null>(null);
  const fileInput = React.useRef<HTMLInputElement>(null);

  const importFile = async (file: File) => {
    try {
      const parsed = SceneImport.safeParse(JSON.parse(await file.text()));
      if (!parsed.success) throw new Error(parsed.error.issues[0]?.message ?? "Invalid scene file");
      const known = new Set((lights.data ?? []).map((l) => l.id));
      const missing = parsed.data.states.filter((s) => !known.has(s.lightId)).map((s) => s.lightId);
      const states = parsed.data.states.filter((s) => known.has(s.lightId));
      if (states.length === 0) throw new Error("None of the scene's lights exist on this controller");
      await create.mutateAsync({ ...parsed.data, states });
      if (missing.length) toast.warning(`Skipped unknown lights: ${missing.join(", ")}`);
    } catch (e) {
      toast.error("Import failed", { description: e instanceof Error ? e.message : String(e) });
    }
  };

  return (
    <>
      <PageHeader
        title="Scenes"
        actions={
          <>
            <input ref={fileInput} type="file" accept="application/json" hidden onChange={(e) => { const f = e.target.files?.[0]; if (f) void importFile(f); e.target.value = ""; }} />
            <Button variant="outline" onClick={() => fileInput.current?.click()}><Upload className="size-4" /> Import</Button>
            <Button variant="outline" onClick={() => setEditing(null)}><Plus className="size-4" /> New</Button>
            <Button onClick={() => setCapturing(true)} disabled={(lights.data ?? []).length === 0}><Sparkles className="size-4" /> Save current state</Button>
          </>
        }
      />
      {scenes.isLoading ? (
        <div className="grid items-start gap-3 sm:grid-cols-2 xl:grid-cols-3">{[0, 1, 2].map((i) => <Skeleton key={i} className="h-32 rounded-xl" />)}</div>
      ) : (scenes.data ?? []).length === 0 ? (
        <EmptyState icon={Sparkles} title="No scenes yet" description="Set your lights the way you like, then save that as a scene." action={<Button onClick={() => setCapturing(true)}>Save current state</Button>} />
      ) : (
        <div className="grid items-start gap-3 sm:grid-cols-2 xl:grid-cols-3">
          {(scenes.data ?? []).map((s) => (
            <div key={s.id} className="flex flex-col justify-between gap-4 rounded-xl border bg-card p-4" style={{ backgroundImage: sceneTint(s) }}>
              <div className="flex items-start justify-between gap-2">
                <div className="min-w-0">
                  <p className="truncate font-medium">{s.name}</p>
                  <p className="text-xs text-muted-foreground">
                    {s.states.length} light{s.states.length === 1 ? "" : "s"} · {(s.transitionMs / 1000).toFixed(1)} s fade
                  </p>
                </div>
                <DropdownMenu>
                  <DropdownMenuTrigger asChild>
                    <Button variant="ghost" size="icon" aria-label={`More actions for ${s.name}`}><MoreVertical className="size-4" /></Button>
                  </DropdownMenuTrigger>
                  <DropdownMenuContent align="end">
                    <DropdownMenuItem onSelect={() => setEditing(s)}><Pencil className="size-4" /> Edit</DropdownMenuItem>
                    <DropdownMenuItem onSelect={() => duplicate.mutate(s.id)}><Copy className="size-4" /> Duplicate</DropdownMenuItem>
                    <DropdownMenuItem onSelect={() => download(`${s.id}.scene.json`, { name: s.name, transitionMs: s.transitionMs, states: s.states })}><Download className="size-4" /> Export</DropdownMenuItem>
                    <DropdownMenuItem className="text-destructive" onSelect={() => setDeleting(s)}><Trash2 className="size-4" /> Delete</DropdownMenuItem>
                  </DropdownMenuContent>
                </DropdownMenu>
              </div>
              <Button size="lg" onClick={() => activate.mutate(s.id, { onSuccess: () => toast.success(`“${s.name}” activated`, { duration: 2000 }) })}>
                <Play className="size-4" /> Activate
              </Button>
            </div>
          ))}
        </div>
      )}
      <CaptureDialog open={capturing} onOpenChange={setCapturing} lights={lights.data ?? []} />
      <SceneEditor scene={editing ?? null} lights={lights.data ?? []} open={editing !== undefined} onOpenChange={(o) => !o && setEditing(undefined)} />
      <ConfirmDialog
        open={deleting !== null}
        onOpenChange={(o) => !o && setDeleting(null)}
        destructive
        title={`Delete “${deleting?.name}”?`}
        description="This cannot be undone."
        confirmLabel="Delete scene"
        onConfirm={() => deleting && del.mutate(deleting.id)}
      />
    </>
  );
}
