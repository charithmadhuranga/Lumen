// Settings (DESIGN §7.11): account, appearance, backup/OTA (see sections
// below), system (restart / factory reset behind typed confirmation), about.
import * as React from "react";
import { useForm } from "react-hook-form";
import { zodResolver } from "@hookform/resolvers/zod";
import { Monitor, Moon, Power, RotateCcw, Sun } from "lucide-react";
import { toast } from "sonner";
import type { z } from "zod";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { ConfirmDialog, PageHeader } from "@/components/common";
import { PasswordFields } from "@/screens/SetupPassword";
import { api } from "@/services/api";
import { useSystemInfo } from "@/services/queries";
import { errorMessage, useAuth } from "@/hooks/useAuth";
import { useTheme } from "@/hooks/useTheme";
import { PasswordChangeForm } from "@/schemas";
import { cn } from "@/lib/utils";
import { BackupSection, FirmwareSection } from "@/screens/settings/Maintenance";

function Section({ title, description, children }: { title: string; description?: string; children: React.ReactNode }) {
  return (
    <section className="space-y-4 rounded-xl border bg-card p-4">
      <div>
        <h2 className="font-semibold">{title}</h2>
        {description && <p className="text-sm text-muted-foreground">{description}</p>}
      </div>
      {children}
    </section>
  );
}

function fmtBytes(n: number) {
  return n > 1024 * 1024 ? `${(n / 1024 / 1024).toFixed(1)} MB` : `${Math.round(n / 1024)} KB`;
}

export default function Settings() {
  const { changePassword } = useAuth();
  const { mode, setMode } = useTheme();
  const info = useSystemInfo();
  const [pwError, setPwError] = React.useState<string | null>(null);
  const [restart, setRestart] = React.useState(false);
  const [reset, setReset] = React.useState(false);
  const [resetText, setResetText] = React.useState("");
  const form = useForm<z.infer<typeof PasswordChangeForm>>({
    resolver: zodResolver(PasswordChangeForm),
    defaultValues: { currentPassword: "", newPassword: "", confirmPassword: "" },
  });

  const submit = form.handleSubmit(async (v) => {
    setPwError(null);
    try {
      await changePassword(v.currentPassword, v.newPassword);
      form.reset();
      toast.success("Password changed — other sessions were signed out", { duration: 4000 });
    } catch (e) {
      setPwError(errorMessage(e));
    }
  });

  const i = info.data;
  return (
    <div className="space-y-6">
      <PageHeader title="Settings" />

      <Section title="Account" description="Changing the password signs out every other session.">
        <form onSubmit={submit} className="max-w-md space-y-4" noValidate>
          <PasswordFields form={form} />
          {pwError && <p className="text-sm text-destructive" role="alert">{pwError}</p>}
          <Button type="submit" disabled={form.formState.isSubmitting}>Change password</Button>
        </form>
      </Section>

      <Section title="Appearance">
        <div className="flex flex-wrap gap-2" role="radiogroup" aria-label="Theme">
          {([["light", Sun, "Light"], ["dark", Moon, "Dark"], ["system", Monitor, "System"]] as const).map(([value, Icon, label]) => (
            <button
              key={value}
              type="button"
              role="radio"
              aria-checked={mode === value}
              onClick={() => setMode(value)}
              className={cn("inline-flex min-h-11 items-center gap-2 rounded-lg border px-4 text-sm", mode === value ? "border-primary bg-primary/10" : "hover:bg-muted")}
            >
              <Icon className="size-4" aria-hidden="true" /> {label}
            </button>
          ))}
        </div>
      </Section>

      <BackupSection />
      <FirmwareSection />

      <Section title="System">
        {i && (
          <dl className="grid grid-cols-1 gap-x-6 gap-y-2 text-sm sm:grid-cols-2">
            {[
              ["Firmware", `${i.firmwareVersion} (${i.buildDate ?? ""})`],
              ["ESP-IDF", i.idfVersion],
              ["Chip", `${i.chipModel} rev ${i.chipRevision / 100} · ${i.cores} cores @ ${i.cpuFreqMHz} MHz`],
              ["Board profile", i.board],
              ["Running slot", i.partition],
              ["Storage", `${fmtBytes(i.fsUsed)} of ${fmtBytes(i.fsTotal)} used`],
              ["Memory", `${fmtBytes(i.freeHeap)} free (lowest ${fmtBytes(i.minFreeHeap)})`],
              ["Uptime", `${Math.floor(i.uptimeSec / 86400)} d ${Math.floor((i.uptimeSec % 86400) / 3600)} h ${Math.floor((i.uptimeSec % 3600) / 60)} min`],
              ["Last reset", i.resetReason.replace(/_/g, " ")],
              ["MAC", i.mac],
            ].map(([k, v]) => (
              <div key={k} className="flex justify-between gap-4 border-b py-1 sm:block sm:border-0">
                <dt className="text-muted-foreground">{k}</dt>
                <dd className="font-medium">{v}</dd>
              </div>
            ))}
          </dl>
        )}
        <div className="flex flex-wrap gap-2">
          <Button variant="outline" onClick={() => setRestart(true)}><Power className="size-4" /> Restart</Button>
          <Button variant="destructive" onClick={() => setReset(true)}><RotateCcw className="size-4" /> Factory reset</Button>
        </div>
      </Section>

      <ConfirmDialog
        open={restart}
        onOpenChange={setRestart}
        title="Restart the controller?"
        description="Lights return to their configured power-up state. The page reconnects automatically."
        confirmLabel="Restart"
        onConfirm={() => api.restart().then(() => toast.info("Restarting…", { duration: 5000 })).catch((e) => toast.error("Restart failed", { description: errorMessage(e) }))}
      />
      <ConfirmDialog
        open={reset}
        onOpenChange={(o) => { setReset(o); setResetText(""); }}
        destructive
        title="Erase all configuration?"
        description="Lights, rooms, scenes, automations, network settings and the admin password are erased. The controller restarts with admin/admin and its setup hotspot. Type RESET to confirm."
        confirmLabel="Erase and restart"
        onConfirm={() => {
          if (resetText !== "RESET") {
            toast.error("Type RESET to confirm");
            return;
          }
          api.factoryReset().then(() => toast.info("Factory reset in progress…", { duration: 8000 })).catch((e) => toast.error("Reset failed", { description: errorMessage(e) }));
        }}
      >
        <Input aria-label="Type RESET to confirm" value={resetText} onChange={(e) => setResetText(e.target.value)} placeholder="RESET" />
      </ConfirmDialog>
    </div>
  );
}
