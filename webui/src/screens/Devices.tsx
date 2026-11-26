// Devices (DESIGN §7.7): Lights (logical channels, add/edit with driver
// picked from real hardware outputs), Inputs (live), Controllers (peers).
import * as React from "react";
import { useForm } from "react-hook-form";
import { zodResolver } from "@hookform/resolvers/zod";
import { CircleDot, Lightbulb, Pencil, Plus, Trash2 } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Badge } from "@/components/ui/badge";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { ConfirmDialog, EmptyState, Field, PageHeader } from "@/components/common";
import { useCreateLight, useDeleteLight, useHardwareStatus, useLights, useRooms, useUpdateLight } from "@/services/queries";
import { useLiveStore } from "@/stores/liveStore";
import { ControllersTab } from "./controllers/ControllersTab";
import { LightCreate, type HardwareOutput, type Light, type LightType } from "@/schemas";

const TYPES: { value: LightType; label: string; outputs: number }[] = [
  { value: "switch", label: "Switch (on/off)", outputs: 1 },
  { value: "dimmer", label: "Dimmer", outputs: 1 },
  { value: "cct", label: "Tunable white (warm + cool)", outputs: 2 },
  { value: "rgb", label: "RGB", outputs: 3 },
  { value: "rgbw", label: "RGBW", outputs: 4 },
];

function slug(s: string) {
  return s.toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 40) || "light";
}

interface DriverOption {
  key: string; // `${type}|${deviceId}|${channel}`
  label: string;
  type: "pca9685" | "relay" | "ledc" | "gpio";
  deviceId: string;
  channel: number;
}

// Driver choices for a light type: for PCA9685 the first output of a run
// of N consecutive free outputs; relays/GPIO only for switches.
function driverOptions(outputs: HardwareOutput[], type: LightType, editing?: Light): DriverOption[] {
  const n = TYPES.find((t) => t.value === type)?.outputs ?? 1;
  const free = (o: HardwareOutput) => !o.lightId || o.lightId === editing?.id;
  const opts: DriverOption[] = [];
  const pca = outputs.filter((o) => o.driver === "pca9685");
  const byDevice = new Map<string, HardwareOutput[]>();
  pca.forEach((o) => {
    const dev = o.id.split(":")[0];
    byDevice.set(dev, [...(byDevice.get(dev) ?? []), o]);
  });
  byDevice.forEach((outs, dev) => {
    for (let i = 0; i + n <= outs.length; i++) {
      const run = outs.slice(i, i + n);
      if (run.every(free)) {
        const ch = Number(run[0].id.split(":")[1]);
        opts.push({
          key: `pca9685|${dev}|${ch}`,
          label: n === 1 ? `${dev} · PWM${ch + 1}` : `${dev} · PWM${ch + 1}–${ch + n}`,
          type: "pca9685",
          deviceId: dev,
          channel: ch,
        });
      }
    }
  });
  if (n === 1) {
    outputs
      .filter((o) => o.driver === "ledc" && free(o))
      .forEach((o) => opts.push({ key: `ledc|${o.id}|0`, label: `${o.id} (native PWM)`, type: "ledc", deviceId: o.id, channel: 0 }));
  }
  if (type === "switch") {
    outputs
      .filter((o) => (o.driver === "relay" || o.driver === "gpio") && free(o))
      .forEach((o) => opts.push({ key: `${o.driver}|${o.id}|0`, label: `${o.name} (${o.driver})`, type: o.driver as "relay" | "gpio", deviceId: o.id, channel: 0 }));
  }
  return opts;
}

function LightDialog({ open, onOpenChange, light }: { open: boolean; onOpenChange: (o: boolean) => void; light?: Light }) {
  const hw = useHardwareStatus();
  const rooms = useRooms();
  const create = useCreateLight();
  const update = useUpdateLight();
  const form = useForm<LightCreate>({
    resolver: zodResolver(LightCreate),
    values: light
      ? { id: light.id, name: light.name, type: light.type, roomId: light.roomId, driver: light.driver, transitionMs: light.transitionMs, startup: light.startup }
      : { id: "", name: "", type: "dimmer", roomId: "", driver: { type: "pca9685", deviceId: "", channel: 0 }, transitionMs: 500, startup: "restore" },
  });
  const type = form.watch("type");
  const driver = form.watch("driver");
  const options = driverOptions(hw.data?.outputs ?? [], type, light);
  const currentKey = `${driver.type}|${driver.deviceId}|${driver.channel}`;
  const e = form.formState.errors;

  const submit = form.handleSubmit(async (v) => {
    if (!v.driver.deviceId) {
      form.setError("driver", { message: "Choose an output" });
      return;
    }
    if (light) {
      const { id: _id, ...patch } = v;
      void _id;
      await update.mutateAsync({ id: light.id, patch });
    } else {
      await create.mutateAsync(v);
    }
    onOpenChange(false);
  });

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-h-[90dvh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>{light ? `Edit “${light.name}”` : "Add light"}</DialogTitle>
          <DialogDescription>A light is a logical channel mapped to one or more hardware outputs.</DialogDescription>
        </DialogHeader>
        <form id="light-form" onSubmit={submit} className="space-y-4">
          <Field label="Name" htmlFor="light-name" error={e.name?.message}>
            <Input
              id="light-name"
              {...form.register("name", {
                onChange: (ev: React.ChangeEvent<HTMLInputElement>) => {
                  if (!light) form.setValue("id", slug(ev.target.value), { shouldValidate: false });
                },
              })}
              autoFocus
            />
          </Field>
          {!light && (
            <Field label="ID" htmlFor="light-id" error={e.id?.message} hint="Used by automations, MQTT and the API">
              <Input id="light-id" className="font-mono" {...form.register("id")} />
            </Field>
          )}
          <Field label="Type">
            <Select value={type} onValueChange={(v) => { form.setValue("type", v as LightType); form.setValue("driver", { type: "pca9685", deviceId: "", channel: 0 }); }}>
              <SelectTrigger aria-label="Light type"><SelectValue /></SelectTrigger>
              <SelectContent>{TYPES.map((t) => <SelectItem key={t.value} value={t.value}>{t.label}</SelectItem>)}</SelectContent>
            </Select>
          </Field>
          <Field label="Output" error={e.driver?.message ?? e.driver?.deviceId?.message} hint={options.length === 0 ? "No free outputs for this type on the active board" : undefined}>
            <Select
              value={driver.deviceId ? currentKey : ""}
              onValueChange={(k) => {
                const o = options.find((x) => x.key === k);
                if (o) form.setValue("driver", { type: o.type, deviceId: o.deviceId, channel: o.channel }, { shouldValidate: true });
              }}
            >
              <SelectTrigger aria-label="Hardware output"><SelectValue placeholder="Choose an output" /></SelectTrigger>
              <SelectContent>{options.map((o) => <SelectItem key={o.key} value={o.key}>{o.label}</SelectItem>)}</SelectContent>
            </Select>
          </Field>
          <Field label="Room">
            <Select value={form.watch("roomId") || "none"} onValueChange={(v) => form.setValue("roomId", v === "none" ? "" : v)}>
              <SelectTrigger aria-label="Room"><SelectValue /></SelectTrigger>
              <SelectContent>
                <SelectItem value="none">No room</SelectItem>
                {(rooms.data ?? []).map((r) => <SelectItem key={r.id} value={r.id}>{r.name}</SelectItem>)}
              </SelectContent>
            </Select>
          </Field>
          <div className="grid grid-cols-2 gap-3">
            <Field label="Default fade (ms)" htmlFor="light-tr" error={e.transitionMs?.message}>
              <Input id="light-tr" type="number" min={0} max={600000} {...form.register("transitionMs", { valueAsNumber: true })} />
            </Field>
            <Field label="At power-up">
              <Select value={form.watch("startup")} onValueChange={(v) => form.setValue("startup", v as LightCreate["startup"])}>
                <SelectTrigger aria-label="Startup behaviour"><SelectValue /></SelectTrigger>
                <SelectContent>
                  <SelectItem value="restore">Restore last state</SelectItem>
                  <SelectItem value="off">Off</SelectItem>
                  <SelectItem value="on">On</SelectItem>
                </SelectContent>
              </Select>
            </Field>
          </div>
        </form>
        <DialogFooter>
          <Button variant="outline" onClick={() => onOpenChange(false)}>Cancel</Button>
          <Button type="submit" form="light-form" disabled={form.formState.isSubmitting}>{light ? "Save" : "Add light"}</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

function LightsTab() {
  const lights = useLights();
  const rooms = useRooms();
  const del = useDeleteLight();
  const [dialog, setDialog] = React.useState<Light | null | undefined>(undefined);
  const [deleting, setDeleting] = React.useState<Light | null>(null);
  const roomName = (id: string) => rooms.data?.find((r) => r.id === id)?.name ?? "—";
  const data = lights.data ?? [];
  return (
    <div className="space-y-4">
      <div className="flex justify-end">
        <Button onClick={() => setDialog(null)}><Plus className="size-4" /> Add light</Button>
      </div>
      {data.length === 0 && !lights.isLoading ? (
        <EmptyState icon={Lightbulb} title="No lights configured" description="Map a hardware output to a light to start controlling it." action={<Button onClick={() => setDialog(null)}>Add light</Button>} />
      ) : (
        <div className="divide-y rounded-xl border bg-card">
          {data.map((l) => (
            <div key={l.id} className="flex flex-wrap items-center gap-3 p-3 sm:flex-nowrap">
              <Lightbulb className={l.state.power ? "size-5 text-primary" : "size-5 text-muted-foreground"} aria-hidden="true" />
              <div className="min-w-0 flex-1">
                <p className="truncate font-medium">{l.name}</p>
                <p className="truncate text-xs text-muted-foreground">
                  <span className="font-mono">{l.id}</span> · {l.type} · {l.driver.deviceId}
                  {l.driver.type === "pca9685" ? ` ch ${l.driver.channel + 1}` : ""} · {roomName(l.roomId)}
                </p>
              </div>
              {l.available ? <Badge variant="secondary">OK</Badge> : <Badge variant="destructive">No hardware</Badge>}
              <Button variant="ghost" size="icon" aria-label={`Edit ${l.name}`} onClick={() => setDialog(l)}><Pencil className="size-4" /></Button>
              <Button variant="ghost" size="icon" aria-label={`Delete ${l.name}`} onClick={() => setDeleting(l)}><Trash2 className="size-4" /></Button>
            </div>
          ))}
        </div>
      )}
      <LightDialog open={dialog !== undefined} onOpenChange={(o) => !o && setDialog(undefined)} light={dialog ?? undefined} />
      <ConfirmDialog
        open={deleting !== null}
        onOpenChange={(o) => !o && setDeleting(null)}
        destructive
        title={`Delete “${deleting?.name}”?`}
        description="The output is switched off and the light is removed from rooms, groups and scenes."
        confirmLabel="Delete light"
        onConfirm={() => deleting && del.mutate(deleting.id)}
      />
    </div>
  );
}

function InputsTab() {
  const hw = useHardwareStatus();
  const live = useLiveStore((s) => s.inputs);
  const inputs = hw.data?.inputs ?? [];
  if (inputs.length === 0) return <EmptyState icon={CircleDot} title="No inputs" description="The active board profile declares no digital inputs." />;
  return (
    <div className="grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-6">
      {inputs.map((i) => {
        const state = live[i.id] ?? i.state;
        return (
          <div key={i.id} className="flex items-center gap-2 rounded-lg border bg-card p-3" aria-live="polite">
            <span className={state ? "size-3 rounded-full bg-success" : "size-3 rounded-full bg-muted-foreground/40"} aria-hidden="true" />
            <div className="min-w-0">
              <p className="text-sm font-medium">{i.name}</p>
              <p className="text-xs text-muted-foreground">{!i.healthy ? "not responding" : state ? "active" : "idle"}</p>
            </div>
          </div>
        );
      })}
    </div>
  );
}

export default function Devices() {
  return (
    <>
      <PageHeader title="Devices" description="Lights, inputs and controllers" />
      <Tabs defaultValue="lights">
        <TabsList>
          <TabsTrigger value="lights">Lights</TabsTrigger>
          <TabsTrigger value="inputs">Inputs</TabsTrigger>
          <TabsTrigger value="controllers">Controllers</TabsTrigger>
        </TabsList>
        <TabsContent value="lights" className="mt-4"><LightsTab /></TabsContent>
        <TabsContent value="inputs" className="mt-4"><InputsTab /></TabsContent>
        <TabsContent value="controllers" className="mt-4"><ControllersTab /></TabsContent>
      </Tabs>
    </>
  );
}
