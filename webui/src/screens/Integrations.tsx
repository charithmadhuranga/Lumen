// Integrations (DESIGN §7.11): MQTT broker, Home Assistant discovery, and the
// local TCP/UDP control servers with their integration token. Secrets are
// write-only (MQTT password) or revealed on explicit request (token).
import * as React from "react";
import { useForm, useWatch } from "react-hook-form";
import { zodResolver } from "@hookform/resolvers/zod";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { Cloud, Copy, Eye, PlugZap, RefreshCw, Unlink } from "lucide-react";
import { toast } from "sonner";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Switch } from "@/components/ui/switch";
import { ConfirmDialog, Field, PageHeader, StatusDot } from "@/components/common";
import { errorMessage } from "@/hooks/useAuth";
import { integrationsApi, MqttForm, type IntegrationsT, type MqttFormT } from "./integrations/api";
import { controllersApi, controllersKey } from "./controllers/api";
import { cloudApi, cloudKey, PairForm, type CloudStatusT, type PairFormT } from "./integrations/cloud";

const KEY = ["integrations"] as const;

function Section({ title, children, actions }: { title: string; children: React.ReactNode; actions?: React.ReactNode }) {
  return (
    <section className="space-y-4 rounded-xl border bg-card p-4">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <h2 className="font-semibold">{title}</h2>
        {actions}
      </div>
      {children}
    </section>
  );
}

function Toggle({ label, checked, onChange, disabled }: { label: string; checked: boolean; onChange: (v: boolean) => void; disabled?: boolean }) {
  return (
    <label className="flex min-h-11 items-center gap-3 text-sm">
      <Switch checked={checked} onCheckedChange={onChange} aria-label={label} disabled={disabled} />
      {label}
    </label>
  );
}

export default function Integrations() {
  // Status counters change while the page is open: poll gently.
  const q = useQuery({ queryKey: KEY, queryFn: integrationsApi.get, refetchInterval: 5000 });
  if (!q.data) return <PageHeader title="Integrations" description={q.error ? errorMessage(q.error) : "Loading…"} />;
  return (
    <div className="space-y-6">
      <PageHeader title="Integrations" description="Connect Lumen to Home Assistant, MQTT and local control clients. Lighting keeps working without them." />
      {/* Re-key the form on settings changes only, not on every status poll. */}
      <MqttSection key={settingsKey(q.data)} data={q.data} />
      <HomeAssistantSection data={q.data} />
      <ControlSection data={q.data} />
      <EspNowSection />
      <CloudSection />
    </div>
  );
}

function settingsKey(d: IntegrationsT) {
  return JSON.stringify({ ...d.mqtt, status: null });
}

function useSave() {
  const qc = useQueryClient();
  return async (fn: () => Promise<IntegrationsT>, ok: string) => {
    try {
      qc.setQueryData(KEY, await fn());
      toast.success(ok, { duration: 2500 });
      return true;
    } catch (e) {
      toast.error("Could not save", { description: errorMessage(e) });
      return false;
    }
  };
}

function MqttSection({ data }: { data: IntegrationsT }) {
  const m = data.mqtt;
  const save = useSave();
  const form = useForm<MqttFormT>({
    resolver: zodResolver(MqttForm),
    defaultValues: {
      enabled: m.enabled,
      host: m.host,
      port: m.port,
      tls: m.tls,
      username: m.username,
      password: "",
      clientId: m.clientId,
      topicPrefix: m.topicPrefix,
      controllerId: m.controllerId,
      qos: m.qos === 1 ? 1 : 0,
      keepalive: m.keepalive,
      retain: m.retain,
    },
  });
  const { register, handleSubmit, setValue, formState, control } = form;
  const [enabled, tls, qos, retain] = useWatch({ control, name: ["enabled", "tls", "qos", "retain"] });
  const err = formState.errors;
  const st = m.status;
  const state = !m.enabled ? "off" : st.connected ? "ok" : st.lastError ? "error" : "warn";
  const label = !m.enabled ? "Disabled" : st.connected ? `Connected · ${st.broker}` : st.lastError ? `Not connected · ${st.lastError}` : "Connecting…";

  const onSubmit = handleSubmit(async ({ password, ...rest }) => {
    await save(() => integrationsApi.updateMqtt({ ...rest, ...(password ? { password } : {}) }), rest.enabled ? "MQTT settings saved — connecting" : "MQTT disabled");
  });

  const test = async () => {
    try {
      const r = await integrationsApi.testMqtt();
      (r.connected ? toast.success : toast.error)(r.message, { duration: 4000 });
    } catch (e) {
      toast.error("Test failed", { description: errorMessage(e) });
    }
  };

  return (
    <Section title="MQTT" actions={<StatusDot status={state} label={label} />}>
      <form onSubmit={(e) => void onSubmit(e)} className="space-y-4" noValidate>
        <Toggle label="Connect to an MQTT broker" checked={enabled} onChange={(v) => setValue("enabled", v, { shouldDirty: true })} />
        <div className="grid gap-4 sm:grid-cols-[1fr_8rem]">
          <Field label="Broker address" htmlFor="mq-host" error={err.host?.message} hint="Hostname or IP, e.g. 192.168.1.10">
            <Input id="mq-host" autoComplete="off" {...register("host")} />
          </Field>
          <Field label="Port" htmlFor="mq-port" error={err.port?.message}>
            <Input id="mq-port" inputMode="numeric" {...register("port", { valueAsNumber: true })} />
          </Field>
        </div>
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label="Username" htmlFor="mq-user" error={err.username?.message}>
            <Input id="mq-user" autoComplete="off" {...register("username")} />
          </Field>
          <Field label="Password" htmlFor="mq-pass" error={err.password?.message} hint={m.hasPassword ? "•••••• (set) — leave empty to keep" : "Stored on the controller, never shown again"}>
            <Input id="mq-pass" type="password" autoComplete="new-password" {...register("password")} />
          </Field>
        </div>
        <Toggle label="Use TLS (mqtts://, verified against built-in root certificates)" checked={tls} onChange={(v) => setValue("tls", v, { shouldDirty: true })} />
        <details className="rounded-lg border p-3">
          <summary className="cursor-pointer text-sm font-medium">Advanced</summary>
          <div className="mt-4 grid gap-4 sm:grid-cols-2">
            <Field label="Topic prefix" htmlFor="mq-prefix" error={err.topicPrefix?.message}>
              <Input id="mq-prefix" {...register("topicPrefix")} />
            </Field>
            <Field label="Controller id" htmlFor="mq-cid" error={err.controllerId?.message} hint="Empty = hostname">
              <Input id="mq-cid" {...register("controllerId")} />
            </Field>
            <Field label="Client id" htmlFor="mq-client" error={err.clientId?.message} hint="Empty = controller id">
              <Input id="mq-client" {...register("clientId")} />
            </Field>
            <Field label="Keepalive (s)" htmlFor="mq-ka" error={err.keepalive?.message}>
              <Input id="mq-ka" inputMode="numeric" {...register("keepalive", { valueAsNumber: true })} />
            </Field>
            <Toggle label="QoS 1 (at least once)" checked={qos === 1} onChange={(v) => setValue("qos", v ? 1 : 0, { shouldDirty: true })} />
            <Toggle label="Retain light states" checked={retain} onChange={(v) => setValue("retain", v, { shouldDirty: true })} />
          </div>
        </details>
        <p className="text-sm text-muted-foreground">
          Topics: <code className="font-mono text-foreground">{data.baseTopic}/light/&lt;id&gt;/set</code> · <code className="font-mono text-foreground">…/state</code> · <code className="font-mono text-foreground">{data.baseTopic}/status</code>
        </p>
        {m.enabled && (
          <p className="text-xs text-muted-foreground">
            Sent {st.published} · received {st.received} · rejected {st.rejected} · reconnects {st.reconnects}
            {st.dropped > 0 ? ` · dropped ${st.dropped}` : ""}
          </p>
        )}
        <div className="flex flex-wrap gap-2">
          <Button type="submit" disabled={formState.isSubmitting}>Save MQTT</Button>
          <Button type="button" variant="outline" onClick={() => void test()} disabled={!m.enabled}><PlugZap className="size-4" /> Test connection</Button>
        </div>
      </form>
    </Section>
  );
}

function HomeAssistantSection({ data }: { data: IntegrationsT }) {
  const ha = data.mqtt.homeAssistant;
  const save = useSave();
  const [prefix, setPrefix] = React.useState(ha.prefix);
  return (
    <Section title="Home Assistant" actions={<StatusDot status={!data.mqtt.enabled ? "off" : ha.enabled && data.mqtt.status.connected ? "ok" : "off"} label={!data.mqtt.enabled ? "Needs MQTT" : ha.enabled ? "Discovery on" : "Discovery off"} />}>
      <p className="text-sm text-muted-foreground">
        With MQTT discovery, every light appears in Home Assistant automatically, with brightness, colour, colour temperature and effects as supported. Deleted lights are removed from Home Assistant too.
      </p>
      <Toggle label="Publish Home Assistant discovery" checked={ha.enabled} disabled={!data.mqtt.enabled} onChange={(enabled) => void save(() => integrationsApi.updateHa({ enabled }), enabled ? "Discovery enabled" : "Discovery disabled — entities removed")} />
      <div className="flex flex-wrap items-end gap-2">
        <div className="w-60">
          <Field label="Discovery prefix" htmlFor="ha-prefix" hint="Home Assistant default: homeassistant">
            <Input id="ha-prefix" value={prefix} onChange={(e) => setPrefix(e.target.value)} maxLength={32} />
          </Field>
        </div>
        <Button variant="outline" disabled={prefix === ha.prefix || !prefix} onClick={() => void save(() => integrationsApi.updateHa({ prefix }), "Discovery prefix saved")}>Save prefix</Button>
      </div>
    </Section>
  );
}

function ControlSection({ data }: { data: IntegrationsT }) {
  const c = data.control;
  const save = useSave();
  const [token, setToken] = React.useState<string | null>(null);
  const [confirmRegen, setConfirmRegen] = React.useState(false);
  const set = (body: Parameters<typeof integrationsApi.updateControl>[0], ok: string) => void save(() => integrationsApi.updateControl(body), ok);

  const reveal = async () => {
    try {
      setToken((await integrationsApi.token()).token);
    } catch (e) {
      toast.error("Could not load the token", { description: errorMessage(e) });
    }
  };
  const copy = async () => {
    if (!token) return;
    try {
      await navigator.clipboard.writeText(token);
      toast.success("Token copied", { duration: 2000 });
    } catch {
      toast.error("Copy is not available here — select the token and copy it manually");
    }
  };
  const regenerate = async () => {
    try {
      setToken((await integrationsApi.regenerateToken()).token);
      toast.success("New token created — update your clients", { duration: 5000 });
    } catch (e) {
      toast.error("Could not regenerate", { description: errorMessage(e) });
    }
  };

  return (
    <Section title="Local control (TCP / UDP)" actions={<StatusDot status={c.tcpEnabled || c.udpControl ? "ok" : "off"} label={c.tcpEnabled ? `TCP on · ${c.status.tcpClients} client${c.status.tcpClients === 1 ? "" : "s"}` : "TCP off"} />}>
      <p className="text-sm text-muted-foreground">
        For custom apps and home-automation bridges on your network. TCP clients sign in with the integration token; UDP commands must be signed with it (HMAC-SHA256). See docs/API.md for the protocol.
      </p>
      <div className="grid gap-x-6 sm:grid-cols-2">
        <Toggle label={`TCP control server (port ${c.tcpPort})`} checked={c.tcpEnabled} onChange={(v) => set({ tcpEnabled: v }, v ? "TCP server on" : "TCP server off")} />
        <Toggle label={`UDP discovery (port ${c.udpPort})`} checked={c.udpEnabled} onChange={(v) => set({ udpEnabled: v }, v ? "UDP on" : "UDP off")} />
        <Toggle label="Accept signed UDP commands" checked={c.udpControl} disabled={!c.udpEnabled} onChange={(v) => set({ udpControl: v }, v ? "Signed UDP commands accepted" : "UDP commands off")} />
        <Toggle label="Broadcast state changes over UDP" checked={c.udpAnnounce} disabled={!c.udpEnabled} onChange={(v) => set({ udpAnnounce: v }, v ? "State broadcasts on" : "State broadcasts off")} />
      </div>
      <p className="text-xs text-muted-foreground">
        TCP commands {c.status.tcpCommands} · sign-in failures {c.status.tcpAuthFailures} · UDP commands {c.status.udpCommands} · rejected {c.status.udpRejected} · discovery replies {c.status.discoveryReplies}
      </p>
      <div className="space-y-2">
        <p className="text-sm font-medium">Integration token</p>
        <div className="flex flex-wrap items-center gap-2">
          {token ? (
            <>
              <code className="break-all rounded bg-muted px-2 py-1 font-mono text-sm" aria-label="Integration token">{token}</code>
              <Button variant="outline" size="sm" onClick={() => void copy()} aria-label="Copy token"><Copy className="size-4" /></Button>
            </>
          ) : (
            <Button variant="outline" onClick={() => void reveal()}><Eye className="size-4" /> Show token</Button>
          )}
          <Button variant="outline" onClick={() => setConfirmRegen(true)}><RefreshCw className="size-4" /> Regenerate</Button>
        </div>
      </div>
      <ConfirmDialog
        open={confirmRegen}
        onOpenChange={setConfirmRegen}
        title="Create a new integration token?"
        description="Every TCP and UDP client using the current token stops working until you give it the new one."
        confirmLabel="Regenerate"
        destructive
        onConfirm={() => void regenerate()}
      />
    </Section>
  );
}

function EspNowSection() {
  const qc = useQueryClient();
  const q = useQuery({ queryKey: controllersKey, queryFn: controllersApi.get, refetchInterval: 10000 });
  const [networkId, setNetworkId] = React.useState<string | null>(null);
  const [keyInput, setKeyInput] = React.useState("");
  const [shownKey, setShownKey] = React.useState<string | null>(null);
  const [confirmRegen, setConfirmRegen] = React.useState(false);
  if (!q.data) return null;
  const self = q.data.self;
  const nid = networkId ?? self.networkId;
  const save = async (body: { enabled?: boolean; networkId?: string; key?: string }, ok: string) => {
    try {
      qc.setQueryData(controllersKey, await controllersApi.update(body));
      toast.success(ok, { duration: 2500 });
      setKeyInput("");
      setNetworkId(null);
    } catch (e) {
      toast.error("Could not save", { description: errorMessage(e) });
    }
  };
  const online = q.data.peers.filter((p) => p.online).length;
  return (
    <Section title="Multi-controller (ESP-NOW)" actions={<StatusDot status={!self.enabled ? "off" : self.running ? "ok" : "warn"} label={!self.enabled ? "Off" : self.running ? `On · ${online} peer${online === 1 ? "" : "s"} online` : "Starting…"} />}>
      <p className="text-sm text-muted-foreground">
        Controllers with the same network id and key see and switch each other's lights directly over the radio — no router, broker or internet needed. All controllers must be on the same Wi-Fi channel (connect them to the same access point).
      </p>
      <Toggle label="Link with other Lumen controllers" checked={self.enabled} onChange={(enabled) => void save({ enabled, networkId: nid }, enabled ? "Multi-controller on" : "Multi-controller off")} />
      <div className="grid gap-4 sm:grid-cols-2">
        <Field label="Network id" htmlFor="en-net" hint="Same on every controller">
          <Input id="en-net" value={nid} onChange={(e) => setNetworkId(e.target.value)} maxLength={32} />
        </Field>
        <Field label="Network key" htmlFor="en-key" hint={self.hasKey ? "•••••• (set) — paste a key to replace it" : "Created automatically when you switch this on"}>
          <Input id="en-key" type="password" autoComplete="off" value={keyInput} onChange={(e) => setKeyInput(e.target.value)} maxLength={64} />
        </Field>
      </div>
      <div className="flex flex-wrap items-center gap-2">
        <Button variant="outline" disabled={!keyInput && nid === self.networkId} onClick={() => void save({ networkId: nid, ...(keyInput ? { key: keyInput } : {}) }, "Saved")}>Save network</Button>
        {shownKey ? (
          <code className="break-all rounded bg-muted px-2 py-1 font-mono text-sm" aria-label="ESP-NOW network key">{shownKey}</code>
        ) : (
          <Button variant="outline" disabled={!self.hasKey} onClick={async () => setShownKey((await controllersApi.key()).key)}><Eye className="size-4" /> Show key</Button>
        )}
        <Button variant="outline" disabled={!self.hasKey} onClick={() => setConfirmRegen(true)}><RefreshCw className="size-4" /> New key</Button>
      </div>
      <p className="text-xs text-muted-foreground">To add a controller: copy this key, then on the other controller set the same network id, paste the key and switch it on. Its lights appear under Devices → Controllers.</p>
      <ConfirmDialog
        open={confirmRegen}
        onOpenChange={setConfirmRegen}
        title="Create a new network key?"
        description="Other controllers stop syncing with this one until they get the new key."
        confirmLabel="Create new key"
        destructive
        onConfirm={async () => {
          try {
            setShownKey((await controllersApi.regenerateKey()).key);
            void qc.invalidateQueries({ queryKey: controllersKey });
          } catch (e) {
            toast.error("Could not create a key", { description: errorMessage(e) });
          }
        }}
      />
    </Section>
  );
}

function cloudDot(s: CloudStatusT): { status: "ok" | "warn" | "off" | "error"; label: string } {
  if (s.state === "paired") return { status: "ok", label: "Paired" };
  if (s.state === "pairing") return { status: "warn", label: "Pairing…" };
  if (s.state === "pair_failed") return { status: "error", label: "Pairing failed" };
  return { status: "off", label: "Off" };
}

function CloudSection() {
  const qc = useQueryClient();
  // Poll quickly while a pairing runs on the controller, gently otherwise.
  const q = useQuery({
    queryKey: cloudKey,
    queryFn: cloudApi.get,
    refetchInterval: (query) => (query.state.data?.state === "pairing" ? 1000 : 10000),
  });
  const [confirmUnpair, setConfirmUnpair] = React.useState(false);
  const form = useForm<PairFormT>({ resolver: zodResolver(PairForm), defaultValues: { claimToken: "", gatewayUrl: "" } });
  const previous = React.useRef<CloudStatusT["state"] | undefined>(undefined);
  React.useEffect(() => {
    const now = q.data?.state;
    if (previous.current === "pairing" && now === "paired") toast.success("Paired with your Lumen Gateway", { duration: 4000 });
    if (previous.current === "pairing" && now === "pair_failed") toast.error("Pairing failed", { description: q.data?.lastError ?? undefined });
    previous.current = now;
  }, [q.data]);
  if (!q.data) return null;
  const s = q.data;
  const dot = cloudDot(s);
  const pair = form.handleSubmit(async (values) => {
    try {
      qc.setQueryData(cloudKey, await cloudApi.pair(values));
      form.reset({ claimToken: "", gatewayUrl: values.gatewayUrl });
    } catch (e) {
      toast.error("Could not start pairing", { description: errorMessage(e) });
    }
  });
  return (
    <Section title="Lumen Cloud" actions={<StatusDot status={dot.status} label={dot.label} />}>
      <p className="text-sm text-muted-foreground">
        Remote access and the Lumen app go through your Lumen Gateway on this network. Lights, scenes and automations keep working without it, and this controller never connects to the internet itself.
      </p>
      {s.state === "paired" ? (
        <dl className="grid gap-x-6 gap-y-2 text-sm sm:grid-cols-2">
          <div><dt className="text-muted-foreground">Gateway</dt><dd className="font-mono break-all">{s.gatewayUrl}</dd></div>
          <div><dt className="text-muted-foreground">Last contact from the gateway</dt><dd>{s.gatewaySeenSecAgo == null ? "Not yet" : s.gatewaySeenSecAgo < 90 ? "Just now" : `${Math.round(s.gatewaySeenSecAgo / 60)} min ago`}</dd></div>
          <div><dt className="text-muted-foreground">Device id</dt><dd className="font-mono text-xs break-all">{s.deviceId}</dd></div>
          <div><dt className="text-muted-foreground">App and gateway access</dt><dd>{s.tickets.accepted} allowed · {s.tickets.rejected} refused</dd></div>
        </dl>
      ) : (
        <form className="space-y-4" onSubmit={(e) => void pair(e)} noValidate>
          {s.state === "pair_failed" && s.lastError ? <p role="alert" className="text-sm text-destructive">{s.lastError}</p> : null}
          <div className="grid gap-4 sm:grid-cols-2">
            <Field label="Claim code" htmlFor="cl-claim" hint="From the Lumen app: Add device → Add with code" error={form.formState.errors.claimToken?.message}>
              <Input id="cl-claim" autoComplete="off" spellCheck={false} {...form.register("claimToken")} />
            </Field>
            <Field label="Gateway address" htmlFor="cl-url" hint="Shown by the app and on the gateway" error={form.formState.errors.gatewayUrl?.message}>
              <Input id="cl-url" placeholder="http://192.168.1.20:8780" autoComplete="off" spellCheck={false} {...form.register("gatewayUrl")} />
            </Field>
          </div>
          <Button type="submit" disabled={s.state === "pairing" || form.formState.isSubmitting}><Cloud className="size-4" /> {s.state === "pairing" ? "Pairing…" : "Pair with gateway"}</Button>
        </form>
      )}
      {s.state === "paired" ? (
        <Button variant="outline" onClick={() => setConfirmUnpair(true)}><Unlink className="size-4" /> Unpair</Button>
      ) : null}
      <ConfirmDialog
        open={confirmUnpair}
        onOpenChange={setConfirmUnpair}
        title="Unpair from the Lumen Gateway?"
        description="The app and remote access stop working for this controller until you pair it again. Local control is not affected."
        confirmLabel="Unpair"
        destructive
        onConfirm={async () => {
          try {
            qc.setQueryData(cloudKey, await cloudApi.unpair());
            toast.success("Unpaired", { duration: 2500 });
          } catch (e) {
            toast.error("Could not unpair", { description: errorMessage(e) });
          }
        }}
      />
    </Section>
  );
}
