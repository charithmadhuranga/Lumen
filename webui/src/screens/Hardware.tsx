// Hardware (DESIGN §7.8): Board, Pin Mapping, Peripherals (I2C scan),
// Outputs (live + test), Inputs, Diagnostics. Built-in profiles are
// read-only: duplicate, edit (JSON with live validation), then apply with
// the confirm -> apply -> success/rollback workflow.
import * as React from "react";
import { useQueryClient } from "@tanstack/react-query";
import { CheckCircle2, Copy, Cpu, Download, Lock, Play, Radar, RotateCcw, Trash2, Upload, XCircle } from "lucide-react";
import { toast } from "sonner";

import { Button } from "@/components/ui/button";
import { Badge } from "@/components/ui/badge";
import { Slider } from "@/components/ui/slider";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { ConfirmDialog, EmptyState, PageHeader, StatusDot } from "@/components/common";
import { PinChip } from "@/components/hardware/PinChip";
import { api } from "@/services/api";
import { queryKeys, useBoard, useBoardPins, useBoards, useDiagnostics, useHardwareStatus } from "@/services/queries";
import { errorMessage } from "@/hooks/useAuth";
import { useLiveStore } from "@/stores/liveStore";
import { BoardProfile, type ApplyResult, type I2cScan, type ValidationReport } from "@/schemas";
import { cn } from "@/lib/utils";

function ReportList({ report }: { report: ValidationReport }) {
  if (report.errors.length === 0 && report.warnings.length === 0) {
    return <p className="flex items-center gap-2 text-sm text-success"><CheckCircle2 className="size-4" /> No problems found</p>;
  }
  return (
    <ul className="space-y-1 text-sm">
      {report.errors.map((e, i) => (
        <li key={`e${i}`} className="flex gap-2 text-destructive"><XCircle className="mt-0.5 size-4 shrink-0" />{e.message}</li>
      ))}
      {report.warnings.map((w, i) => (
        <li key={`w${i}`} className="flex gap-2 text-foreground"><span className="mt-1.5 size-2 shrink-0 rounded-full bg-warning" />{w.message}</li>
      ))}
    </ul>
  );
}

function download(name: string, data: unknown) {
  const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: "application/json" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  URL.revokeObjectURL(url);
}

/* --------------------------------------------------------------- Board */
function profileText(p: BoardProfile | undefined): string {
  if (!p) return "";
  const { builtIn: _b, active: _a, ...doc } = p;
  void _b;
  void _a;
  return JSON.stringify(doc, null, 2);
}

function BoardTab({ selected, onSelect }: { selected: string | null; onSelect: (id: string) => void }) {
  const board = useBoard(selected);
  return (
    <BoardEditor
      key={`${selected}:${board.dataUpdatedAt}`}
      selected={selected}
      onSelect={onSelect}
      initialText={profileText(board.data)}
    />
  );
}

function BoardEditor({ selected, onSelect, initialText }: { selected: string | null; onSelect: (id: string) => void; initialText: string }) {
  const qc = useQueryClient();
  const boards = useBoards();
  const [text, setText] = React.useState(initialText);
  const [report, setReport] = React.useState<ValidationReport | null>(null);
  const [confirmApply, setConfirmApply] = React.useState(false);
  const [applying, setApplying] = React.useState(false);
  const [result, setResult] = React.useState<ApplyResult | null>(null);
  const [deleting, setDeleting] = React.useState(false);
  const fileInput = React.useRef<HTMLInputElement>(null);
  const current = boards.data?.find((b) => b.id === selected);

  const parse = (): BoardProfile | null => {
    try {
      const r = BoardProfile.safeParse(JSON.parse(text));
      if (!r.success) {
        toast.error("Profile does not match the schema", { description: r.error.issues[0]?.path.join(".") + ": " + r.error.issues[0]?.message });
        return null;
      }
      return r.data;
    } catch (e) {
      toast.error("Invalid JSON", { description: errorMessage(e) });
      return null;
    }
  };
  const refresh = () => {
    void qc.invalidateQueries({ queryKey: ["boards"] });
    void qc.invalidateQueries({ queryKey: ["hardware"] });
    void qc.invalidateQueries({ queryKey: queryKeys.lights() });
  };

  const validate = async () => {
    const p = parse();
    if (!p) return;
    try {
      setReport(await api.validateBoard(p));
    } catch (e) {
      toast.error("Validation failed", { description: errorMessage(e) });
    }
  };
  const save = async () => {
    const p = parse();
    if (!p || !current) return;
    try {
      await api.updateBoard(current.id, p);
      toast.success("Profile saved", { duration: 2500 });
      refresh();
    } catch (e) {
      toast.error("Could not save", { description: errorMessage(e) });
    }
  };
  const duplicate = async () => {
    if (!current) return;
    try {
      const copy = await api.duplicateBoard(current.id);
      toast.success(`Created “${copy.name}”`, { duration: 2500 });
      refresh();
      onSelect(copy.id);
    } catch (e) {
      toast.error("Could not duplicate", { description: errorMessage(e) });
    }
  };
  const importFile = async (file: File) => {
    try {
      const r = BoardProfile.safeParse(JSON.parse(await file.text()));
      if (!r.success) throw new Error(r.error.issues[0]?.message ?? "Invalid profile");
      const created = await api.createBoard({ ...r.data, builtIn: undefined, active: undefined });
      toast.success(`Imported “${created.name}”`, { duration: 2500 });
      refresh();
      onSelect(created.id);
    } catch (e) {
      toast.error("Import failed", { description: errorMessage(e) });
    }
  };
  const apply = async () => {
    if (!current) return;
    setApplying(true);
    setResult(null);
    try {
      setResult(await api.applyBoard(current.id));
    } catch (e) {
      const data = (e as { data?: unknown }).data;
      setResult({ applied: false, rolled_back: false, active_board: "", message: errorMessage(e), report: data as ValidationReport | undefined });
    } finally {
      setApplying(false);
      refresh();
    }
  };

  return (
    <div className="grid gap-6 lg:grid-cols-[18rem_1fr]">
      <div className="space-y-2">
        {(boards.data ?? []).map((b) => (
          <button
            key={b.id}
            type="button"
            onClick={() => onSelect(b.id)}
            className={cn(
              "flex w-full items-start gap-2 rounded-lg border bg-card p-3 text-left hover:border-primary focus-visible:outline-none focus-visible:ring-3 focus-visible:ring-ring/50",
              selected === b.id && "border-primary ring-1 ring-primary",
            )}
          >
            {b.builtIn ? <Lock className="mt-0.5 size-4 text-muted-foreground" aria-label="Built-in (read-only)" /> : <Cpu className="mt-0.5 size-4 text-muted-foreground" aria-hidden="true" />}
            <span className="min-w-0 flex-1">
              <span className="block truncate text-sm font-medium">{b.name}</span>
              <span className="block text-xs text-muted-foreground">{b.mcu}{b.unverified ? " · partly unverified" : ""}</span>
            </span>
            {b.active && <Badge>Active</Badge>}
          </button>
        ))}
        <input ref={fileInput} type="file" accept="application/json" hidden onChange={(e) => { const f = e.target.files?.[0]; if (f) void importFile(f); e.target.value = ""; }} />
        <Button variant="outline" className="w-full" onClick={() => fileInput.current?.click()}><Upload className="size-4" /> Import profile</Button>
      </div>

      {current && (
        <div className="space-y-4">
          <div className="flex flex-wrap items-center gap-2">
            <h2 className="mr-auto text-lg font-semibold">{current.name}</h2>
            <Button variant="outline" onClick={() => void duplicate()}><Copy className="size-4" /> Duplicate</Button>
            <Button variant="outline" onClick={() => text && download(`${current.id}.board.json`, JSON.parse(text))}><Download className="size-4" /> Export</Button>
            {!current.builtIn && !current.active && (
              <Button variant="destructive" onClick={() => setDeleting(true)}><Trash2 className="size-4" /> Delete</Button>
            )}
            {!current.active && <Button onClick={() => setConfirmApply(true)} disabled={applying}><Play className="size-4" /> Apply</Button>}
          </div>
          {current.desc && <p className="text-sm text-muted-foreground">{current.desc}</p>}
          {current.builtIn && (
            <p className="rounded-md bg-muted px-3 py-2 text-sm">Built-in profiles are read-only. Duplicate it to customise pins and peripherals.</p>
          )}
          <label htmlFor="profile-json" className="text-sm font-medium">Profile document</label>
          <textarea
            id="profile-json"
            spellCheck={false}
            readOnly={current.builtIn}
            value={text}
            onChange={(e) => setText(e.target.value)}
            className="h-80 w-full rounded-lg border bg-background p-3 font-mono text-xs focus-visible:outline-none focus-visible:ring-3 focus-visible:ring-ring/50"
          />
          <div className="flex flex-wrap gap-2">
            <Button variant="outline" onClick={() => void validate()}>Validate</Button>
            {!current.builtIn && <Button onClick={() => void save()}>Save</Button>}
          </div>
          {report && (
            <div className="space-y-3 rounded-lg border p-3">
              <ReportList report={report} />
              {report.pins && (
                <div role="list" aria-label="Pin map preview" className="grid grid-cols-3 gap-2 sm:grid-cols-5 xl:grid-cols-8">
                  {report.pins.filter((p) => p.owner || p.conflict).map((p) => <PinChip key={p.gpio} pin={p} />)}
                </div>
              )}
            </div>
          )}
          {result && (
            <div role="status" className={cn("rounded-lg border p-3 text-sm", result.applied ? "border-success/50 bg-success/10" : "border-destructive/50 bg-destructive/10")}>
              <p className="flex items-center gap-2 font-medium">
                {result.applied ? <CheckCircle2 className="size-4 text-success" /> : result.rolled_back ? <RotateCcw className="size-4 text-destructive" /> : <XCircle className="size-4 text-destructive" />}
                {result.applied ? "Profile applied" : result.rolled_back ? "Apply failed — previous profile restored" : "Profile not applied"}
              </p>
              <p className="mt-1">{result.message}</p>
              {result.report && result.report.errors.length > 0 && <div className="mt-2"><ReportList report={result.report} /></div>}
            </div>
          )}
        </div>
      )}
      <ConfirmDialog
        open={confirmApply}
        onOpenChange={setConfirmApply}
        title={`Apply “${current?.name}”?`}
        description={
          <>
            All outputs go to their safe (off) state while the hardware is re-initialised. If any peripheral fails its check, the
            current profile is restored automatically. Lights referencing outputs that don't exist on the new board become unavailable.
          </>
        }
        confirmLabel="Apply profile"
        onConfirm={() => void apply()}
      />
      <ConfirmDialog
        open={deleting}
        onOpenChange={setDeleting}
        destructive
        title={`Delete “${current?.name}”?`}
        description="The custom profile is removed from the controller."
        confirmLabel="Delete profile"
        onConfirm={() => current && api.deleteBoard(current.id).then(() => { refresh(); onSelect(boards.data?.find((b) => b.active)?.id ?? ""); }).catch((e) => toast.error("Could not delete", { description: errorMessage(e) }))}
      />
    </div>
  );
}

/* ------------------------------------------------------------ Pin map */
function PinsTab({ boardId }: { boardId: string | null }) {
  const pins = useBoardPins(boardId);
  if (!boardId) return null;
  return (
    <div className="space-y-3">
      <div className="flex flex-wrap gap-4 text-xs text-muted-foreground">
        <span className="flex items-center gap-1"><span className="size-3 rounded border border-primary/40 bg-primary/10" /> assigned</span>
        <span className="flex items-center gap-1"><span className="size-3 rounded border border-destructive bg-destructive/10" /> conflict</span>
        <span className="flex items-center gap-1"><span className="size-3 rounded bg-muted" /> reserved (flash)</span>
        <span className="text-warning">strap</span> = boot-strapping pin
      </div>
      <div role="list" aria-label="GPIO pins" className="grid grid-cols-3 gap-2 sm:grid-cols-5 lg:grid-cols-8">
        {(pins.data ?? []).map((p) => <PinChip key={p.gpio} pin={p} />)}
      </div>
    </div>
  );
}

/* ---------------------------------------------------------- Peripherals */
function PeripheralsTab() {
  const status = useHardwareStatus(true);
  const [scan, setScan] = React.useState<I2cScan | null>(null);
  const [scanning, setScanning] = React.useState(false);
  const run = async () => {
    setScanning(true);
    try {
      setScan(await api.i2cScan());
    } catch (e) {
      toast.error("I2C scan failed", { description: errorMessage(e) });
    } finally {
      setScanning(false);
    }
  };
  return (
    <div className="space-y-6">
      <div className="divide-y rounded-xl border bg-card">
        {(status.data?.peripherals ?? []).map((p) => (
          <div key={p.id} className="flex items-center gap-3 p-3">
            <StatusDot status={p.ok ? "ok" : "error"} label={p.ok ? "OK" : "Fault"} />
            <span className="flex-1 font-medium">{p.id}</span>
            <span className="text-sm text-muted-foreground">{p.type}</span>
            {p.errors > 0 && <Badge variant="destructive">{p.errors} errors</Badge>}
            {!p.ok && <span className="text-xs text-destructive">{p.detail}</span>}
          </div>
        ))}
      </div>
      <div className="space-y-3">
        <div className="flex items-center gap-3">
          <Button onClick={() => void run()} disabled={scanning}><Radar className="size-4" /> {scanning ? "Scanning…" : "Scan I2C bus"}</Button>
          {scan && <span className="text-sm text-muted-foreground">SDA GPIO{scan.sda_gpio} · SCL GPIO{scan.scl_gpio}</span>}
        </div>
        {scan && (
          <div className="space-y-2">
            <div className="flex flex-wrap gap-2">
              {scan.devices.map((d) => (
                <span key={d.address} className={cn("rounded-lg border px-3 py-2 text-sm", d.unexpected ? "border-warning bg-warning/10" : "bg-card")}>
                  <span className="font-mono font-semibold">{d.address_hex}</span> {d.label ?? "unknown device"}
                </span>
              ))}
              {scan.devices.length === 0 && <p className="text-sm text-muted-foreground">No devices answered.</p>}
            </div>
            {scan.missing.length > 0 && <p className="text-sm text-destructive">Declared but not found: {scan.missing.join(", ")}</p>}
          </div>
        )}
      </div>
    </div>
  );
}

/* -------------------------------------------------------------- Outputs */
function OutputsTab() {
  const status = useHardwareStatus(true);
  const qc = useQueryClient();
  const test = async (id: string, level: number) => {
    try {
      await api.testOutput(id, level);
      void qc.invalidateQueries({ queryKey: queryKeys.hardwareStatus() });
    } catch (e) {
      toast.error("Output test failed", { description: errorMessage(e) });
    }
  };
  return (
    <div className="divide-y rounded-xl border bg-card">
      {(status.data?.outputs ?? []).map((o) => (
        <div key={o.id} className="flex flex-wrap items-center gap-3 p-3 sm:flex-nowrap">
          <span className={o.state ? "size-2.5 rounded-full bg-primary" : "size-2.5 rounded-full bg-muted-foreground/40"} aria-hidden="true" />
          <span className="w-36 font-medium">{o.name}</span>
          <span className="flex-1 truncate text-sm text-muted-foreground">{o.lightName ? `used by ${o.lightName}` : "free"}</span>
          {o.type === "pwm" ? (
            <div className="flex w-56 items-center gap-2">
              <Slider value={[o.value]} min={0} max={100} step={1} aria-label={`Test level for ${o.name}`} className="py-3" onValueCommit={([v]) => void test(o.id, v)} />
              <span className="w-10 text-right text-xs tabular-nums">{o.value}%</span>
            </div>
          ) : (
            <Button size="sm" variant="outline" onClick={() => void test(o.id, o.state ? 0 : 100)}>{o.state ? "Turn off" : "Turn on"}</Button>
          )}
        </div>
      ))}
    </div>
  );
}

function InputsTab() {
  const status = useHardwareStatus(true);
  const live = useLiveStore((s) => s.inputs);
  return (
    <div className="grid grid-cols-2 gap-2 sm:grid-cols-4 lg:grid-cols-6">
      {(status.data?.inputs ?? []).map((i) => {
        const state = live[i.id] ?? i.state;
        return (
          <div key={i.id} className="rounded-lg border bg-card p-3">
            <p className="text-sm font-medium">{i.name}</p>
            <p className="font-mono text-[11px] text-muted-foreground">{i.id}</p>
            <p className={cn("mt-1 text-xs", state ? "text-success" : "text-muted-foreground")}>{!i.healthy ? "no response" : state ? "ACTIVE" : "idle"}</p>
          </div>
        );
      })}
    </div>
  );
}

function DiagnosticsTab({ active }: { active: boolean }) {
  const diag = useDiagnostics(active);
  const d = diag.data as Record<string, unknown> | undefined;
  if (!d) return <p className="text-sm text-muted-foreground">Loading diagnostics…</p>;
  const heap = d.heap as { free: number; minFree: number; largestBlock: number };
  const lighting = d.lighting as Record<string, number>;
  const tasks = d.tasks as { name: string; stackSize: number; stackFreeMin: number }[];
  const web = d.web as Record<string, number>;
  const stat = (label: string, value: string) => (
    <div className="rounded-lg border bg-card p-3">
      <p className="text-xs text-muted-foreground">{label}</p>
      <p className="text-lg font-semibold tabular-nums">{value}</p>
    </div>
  );
  return (
    <div className="space-y-4">
      <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
        {stat("Free heap", `${Math.round(heap.free / 1024)} KB`)}
        {stat("Lowest free heap", `${Math.round(heap.minFree / 1024)} KB`)}
        {stat("Largest block", `${Math.round(heap.largestBlock / 1024)} KB`)}
        {stat("Uptime", `${Math.floor(Number(d.uptimeSec) / 3600)} h ${Math.floor((Number(d.uptimeSec) % 3600) / 60)} m`)}
        {stat("Lighting tick (max)", `${(lighting.maxTickUs / 1000).toFixed(1)} ms`)}
        {stat("Hardware errors", String(lighting.hardwareErrors))}
        {stat("Commands dropped", String(lighting.queueDropped))}
        {stat("Live clients", String(web.wsClients))}
      </div>
      <div className="rounded-lg border bg-card p-3">
        <p className="mb-2 text-sm font-medium">Task stacks</p>
        {tasks.map((t) => (
          <p key={t.name} className="text-sm">{t.name}: {t.stackFreeMin} of {t.stackSize} bytes free (minimum)</p>
        ))}
      </div>
    </div>
  );
}

export default function Hardware() {
  const boards = useBoards();
  const [tab, setTab] = React.useState("board");
  const [picked, setSelected] = React.useState<string | null>(null);
  const selected = picked ?? boards.data?.find((b) => b.active)?.id ?? boards.data?.[0]?.id ?? null;
  if (boards.isError) return <EmptyState icon={Cpu} title="Hardware unavailable" description={errorMessage(boards.error)} />;
  return (
    <>
      <PageHeader title="Hardware" description="Board profile, pins, peripherals and diagnostics" />
      <Tabs value={tab} onValueChange={setTab}>
        <TabsList className="flex-wrap">
          <TabsTrigger value="board">Board</TabsTrigger>
          <TabsTrigger value="pins">Pin mapping</TabsTrigger>
          <TabsTrigger value="peripherals">Peripherals</TabsTrigger>
          <TabsTrigger value="outputs">Outputs</TabsTrigger>
          <TabsTrigger value="inputs">Inputs</TabsTrigger>
          <TabsTrigger value="diagnostics">Diagnostics</TabsTrigger>
        </TabsList>
        <TabsContent value="board" className="mt-4"><BoardTab selected={selected} onSelect={setSelected} /></TabsContent>
        <TabsContent value="pins" className="mt-4"><PinsTab boardId={selected} /></TabsContent>
        <TabsContent value="peripherals" className="mt-4"><PeripheralsTab /></TabsContent>
        <TabsContent value="outputs" className="mt-4"><OutputsTab /></TabsContent>
        <TabsContent value="inputs" className="mt-4"><InputsTab /></TabsContent>
        <TabsContent value="diagnostics" className="mt-4"><DiagnosticsTab active={tab === "diagnostics"} /></TabsContent>
      </Tabs>
    </>
  );
}
