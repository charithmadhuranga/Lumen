// Settings → Firmware, Web interface, Backup & restore (DESIGN §7.11, SPEC §20).
// Uploads stream straight to the controller (XHR for progress). The firmware
// verifies every image before it can boot and rolls back on a failed start;
// a restore is applied transactionally and needs a restart.
import * as React from "react";
import { useQuery } from "@tanstack/react-query";
import { Download, HardDriveUpload, Upload } from "lucide-react";
import { toast } from "sonner";
import { z } from "zod";

import { Button } from "@/components/ui/button";
import { Switch } from "@/components/ui/switch";
import { ConfirmDialog, StatusDot } from "@/components/common";
import { errorMessage } from "@/hooks/useAuth";
import { api, request, upload } from "@/services/api";

const Slot = z.object({ partition: z.string(), version: z.string(), state: z.string() });
const OtaStatus = z.object({
  state: z.string(),
  received: z.number(),
  total: z.number(),
  error: z.string(),
  newVersion: z.string(),
  running: Slot,
  other: Slot,
  slotSize: z.number(),
  rollbackPossible: z.boolean(),
});
type OtaStatusT = z.infer<typeof OtaStatus>;
const OtaResult = OtaStatus.extend({ restarting: z.boolean() });
const UiResult = z.object({ assets: z.number(), bytes: z.number() });
const RestoreResult = z.object({ restored: z.array(z.string()), restarting: z.boolean() });
const Backup = z.object({ format: z.literal("lumen-backup"), version: z.number(), documents: z.record(z.string(), z.unknown()) }).passthrough();

function Section({ title, description, actions, children }: { title: string; description?: string; actions?: React.ReactNode; children: React.ReactNode }) {
  return (
    <section className="space-y-4 rounded-xl border bg-card p-4">
      <div className="flex flex-wrap items-start justify-between gap-2">
        <div>
          <h2 className="font-semibold">{title}</h2>
          {description && <p className="text-sm text-muted-foreground">{description}</p>}
        </div>
        {actions}
      </div>
      {children}
    </section>
  );
}

/** Transform-only progress bar (DESIGN §11: animate transform/opacity only). */
function Progress({ value, label }: { value: number; label: string }) {
  const pct = Math.round(value * 100);
  return (
    <div className="space-y-1">
      <div className="h-2 overflow-hidden rounded-full bg-muted" role="progressbar" aria-label={label} aria-valuemin={0} aria-valuemax={100} aria-valuenow={pct}>
        <div className="h-full origin-left bg-primary transition-transform" style={{ transform: `scaleX(${value})` }} />
      </div>
      <p className="text-xs text-muted-foreground">{pct} %</p>
    </div>
  );
}

function FilePicker({ id, accept, label, file, onFile, disabled }: { id: string; accept: string; label: string; file: File | null; onFile: (f: File | null) => void; disabled?: boolean }) {
  return (
    <div className="flex flex-wrap items-center gap-3">
      <label htmlFor={id} className="inline-flex min-h-11 cursor-pointer items-center gap-2 rounded-md border px-3 text-sm hover:border-primary">
        <Upload className="size-4" aria-hidden="true" /> {label}
      </label>
      <input id={id} type="file" accept={accept} className="sr-only" disabled={disabled} onChange={(e) => onFile(e.target.files?.[0] ?? null)} />
      <span className="text-sm text-muted-foreground">{file ? `${file.name} · ${Math.round(file.size / 1024)} KB` : "No file chosen"}</span>
    </div>
  );
}

/** Polls /system/health until the controller answers again, then reloads. */
async function waitForRestart(message: string) {
  const id = toast.loading(message);
  await new Promise((r) => setTimeout(r, 5000));
  for (let i = 0; i < 60; i++) {
    try {
      await api.health();
      toast.success("The controller is back", { id, duration: 2000 });
      setTimeout(() => window.location.reload(), 800);
      return;
    } catch {
      await new Promise((r) => setTimeout(r, 2000));
    }
  }
  toast.error("The controller did not come back within two minutes", { id, description: "Check its power and network, then reload this page." });
}

const slotLabel: Record<string, string> = {
  valid: "confirmed",
  pending_verify: "checking — confirms itself after a healthy start",
  invalid: "failed its start, not bootable",
  aborted: "failed its start, not bootable",
  new: "installed, not started yet",
  undefined: "factory / serial flash",
};

export function FirmwareSection() {
  const q = useQuery({ queryKey: ["ota-status"], queryFn: () => request("GET", "/ota/status", OtaStatus), refetchInterval: 10000 });
  const [file, setFile] = React.useState<File | null>(null);
  const [progress, setProgress] = React.useState<number | null>(null);
  const [confirm, setConfirm] = React.useState(false);
  const s: OtaStatusT | undefined = q.data;

  const install = async () => {
    if (!file) return;
    setProgress(0);
    try {
      const r = await upload("/ota/upload", file, OtaResult, setProgress);
      setProgress(1);
      void waitForRestart(`Firmware ${r.newVersion} verified — restarting…`);
    } catch (e) {
      setProgress(null);
      toast.error("Firmware not installed", { description: errorMessage(e), duration: 10000 });
      void q.refetch();
    }
  };

  return (
    <Section
      title="Firmware"
      description="Install a firmware.bin. It is verified before it can start; if the new firmware does not start correctly the controller returns to the current one. Settings are kept."
      actions={s && <StatusDot status={s.running.state === "pending_verify" ? "warn" : "ok"} label={`Running ${s.running.version}`} />}
    >
      {s && (
        <dl className="grid gap-x-6 gap-y-1 text-sm sm:grid-cols-[auto_1fr]">
          <dt className="text-muted-foreground">Running</dt>
          <dd>
            {s.running.version} · slot {s.running.partition} · {slotLabel[s.running.state] ?? s.running.state}
          </dd>
          <dt className="text-muted-foreground">Other slot</dt>
          <dd>{s.other.version ? `${s.other.version} · ${slotLabel[s.other.state] ?? s.other.state}` : "empty"}</dd>
          {s.state === "error" && s.error && (
            <>
              <dt className="text-muted-foreground">Last attempt</dt>
              <dd className="text-destructive">{s.error}</dd>
            </>
          )}
        </dl>
      )}
      <FilePicker id="fw-file" accept=".bin,application/octet-stream" label="Choose firmware" file={file} onFile={setFile} disabled={progress !== null} />
      {progress !== null && <Progress value={progress} label="Firmware upload" />}
      <Button onClick={() => setConfirm(true)} disabled={!file || progress !== null || s?.running.state === "pending_verify"}>
        <HardDriveUpload className="size-4" /> Install firmware
      </Button>
      {s?.running.state === "pending_verify" && <p className="text-xs text-muted-foreground">The running firmware is still confirming itself; updates are possible again in a few seconds.</p>}
      <ConfirmDialog
        open={confirm}
        onOpenChange={setConfirm}
        title="Install this firmware?"
        description={`${file?.name ?? ""} will be verified and the controller restarts (about 15 s). Lights keep their state; this page reconnects by itself.`}
        confirmLabel="Install and restart"
        onConfirm={() => void install()}
      />
    </Section>
  );
}

export function WebInterfaceSection() {
  const [file, setFile] = React.useState<File | null>(null);
  const [progress, setProgress] = React.useState<number | null>(null);
  const install = async () => {
    if (!file) return;
    setProgress(0);
    try {
      const r = await upload("/system/ui/upload", file, UiResult, setProgress);
      toast.success(`Web interface installed (${r.assets} files) — reloading`, { duration: 2000 });
      setTimeout(() => window.location.reload(), 1200);
    } catch (e) {
      setProgress(null);
      toast.error("Web interface not installed", { description: `${errorMessage(e)} If this page stops working, open /recovery.`, duration: 12000 });
    }
  };
  return (
    <Section title="Web interface" description="Install a web.pack built with npm run build:fs. If an update is interrupted, the built-in page at /recovery can install it again.">
      <FilePicker id="ui-file" accept=".pack" label="Choose web.pack" file={file} onFile={setFile} disabled={progress !== null} />
      {progress !== null && <Progress value={progress} label="Web interface upload" />}
      <Button variant="outline" onClick={() => void install()} disabled={!file || progress !== null}>
        Install web interface
      </Button>
    </Section>
  );
}

export function BackupSection() {
  const [file, setFile] = React.useState<File | null>(null);
  const [merge, setMerge] = React.useState(false);
  const [confirm, setConfirm] = React.useState(false);
  const [busy, setBusy] = React.useState(false);

  const download = async () => {
    try {
      const b = await request("GET", "/backup", Backup, undefined, { timeoutMs: 30000 });
      const blob = new Blob([JSON.stringify(b, null, 2)], { type: "application/json" });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = `lumen-backup-${new Date().toISOString().slice(0, 10)}.json`;
      a.click();
      URL.revokeObjectURL(url);
      toast.success(`Backup downloaded (${Object.keys(b.documents).length} documents)`, { duration: 3000 });
    } catch (e) {
      toast.error("Backup failed", { description: errorMessage(e) });
    }
  };

  const restore = async () => {
    if (!file) return;
    setBusy(true);
    try {
      const r = await upload(`/backup/restore${merge ? "?mode=merge" : ""}`, file, RestoreResult);
      void waitForRestart(`Restored ${r.restored.length} documents — restarting…`);
    } catch (e) {
      setBusy(false);
      toast.error("Nothing was restored", { description: errorMessage(e), duration: 10000 });
    }
  };

  return (
    <Section title="Backup & restore" description="Lights, rooms, groups, scenes, automations, hardware and integration settings. Passwords and keys are never included — re-enter Wi-Fi and MQTT passwords after restoring on another controller.">
      <Button variant="outline" onClick={() => void download()}>
        <Download className="size-4" /> Download backup
      </Button>
      <div className="space-y-3 border-t pt-4">
        <FilePicker id="restore-file" accept=".json,application/json" label="Choose backup" file={file} onFile={setFile} disabled={busy} />
        <label className="flex min-h-11 items-center gap-3 text-sm">
          <Switch checked={merge} onCheckedChange={setMerge} aria-label="Only replace the documents in the backup" />
          Only replace what the backup contains (keep everything else)
        </label>
        <Button variant="destructive" onClick={() => setConfirm(true)} disabled={!file || busy}>
          Restore…
        </Button>
      </div>
      <ConfirmDialog
        open={confirm}
        onOpenChange={setConfirm}
        title="Restore this backup?"
        description={merge ? "Documents in the backup replace the current ones; everything else is kept. The controller then restarts." : "The current configuration is replaced by the backup. The controller then restarts."}
        confirmLabel="Restore and restart"
        destructive
        onConfirm={() => void restore()}
      />
    </Section>
  );
}
