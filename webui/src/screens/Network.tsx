// Network (DESIGN §7.10): Wi-Fi (scan, SSID, password, DHCP/static), access
// point policy, Ethernet, hostname/time. Changes that can drop the UI need a
// confirm dialog explaining how to reconnect.
import * as React from "react";
import { Eye, Radar, Wifi } from "lucide-react";
import { toast } from "sonner";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Switch } from "@/components/ui/switch";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { ConfirmDialog, Field, PageHeader, StatusDot } from "@/components/common";
import { api } from "@/services/api";
import { useNetwork, useNetworkStatus } from "@/services/queries";
import { errorMessage } from "@/hooks/useAuth";
import type { NetworkSettings, NetworkStatus, WifiNetwork } from "@/schemas";

const POLICIES: Record<NetworkSettings["ap"]["policy"], string> = {
  sta_fallback_ap: "Wi-Fi, with setup hotspot when Wi-Fi is unreachable",
  sta_only: "Wi-Fi only",
  sta_and_ap: "Wi-Fi and hotspot together",
  ap_only: "Hotspot only",
};

function Section({ title, children, actions }: { title: string; children: React.ReactNode; actions?: React.ReactNode }) {
  return (
    <section className="space-y-4 rounded-xl border bg-card p-4">
      <div className="flex items-center justify-between gap-2">
        <h2 className="font-semibold">{title}</h2>
        {actions}
      </div>
      {children}
    </section>
  );
}

export default function Network() {
  const settings = useNetwork();
  const status = useNetworkStatus();
  if (!settings.data || !status.data) return <PageHeader title="Network" description="Loading…" />;
  return <NetworkForm key={settings.dataUpdatedAt} s={settings.data} st={status.data} refetch={() => void settings.refetch()} />;
}

function NetworkForm({ s, st, refetch }: { s: NetworkSettings; st: NetworkStatus; refetch: () => void }) {
  const [ssid, setSsid] = React.useState(s.wifi.ssid);
  const [password, setPassword] = React.useState("");
  const [staticIp, setStaticIp] = React.useState({ ...s.wifi.staticIp, netmask: s.wifi.staticIp.netmask || "255.255.255.0" });
  const [networks, setNetworks] = React.useState<WifiNetwork[] | null>(null);
  const [scanning, setScanning] = React.useState(false);
  const [confirmWifi, setConfirmWifi] = React.useState(false);
  const [policy, setPolicy] = React.useState<NetworkSettings["ap"]["policy"]>(s.ap.policy);
  const [apPassword, setApPassword] = React.useState("");
  const [shownApPassword, setShownApPassword] = React.useState<string | null>(null);
  const [hostname, setHostname] = React.useState(s.hostname);
  const [timezone, setTimezone] = React.useState(s.timezone);

  const scan = async () => {
    setScanning(true);
    try {
      setNetworks(await api.wifiScan());
    } catch (e) {
      toast.error("Scan failed", { description: errorMessage(e) });
    } finally {
      setScanning(false);
    }
  };

  const applyWifi = async () => {
    try {
      await api.updateWifi({ ssid, ...(password ? { password } : {}), staticIp });
      toast.success("Wi-Fi settings sent", {
        description: `The controller is reconnecting. If this page stops responding, open http://${s.hostname}.local/ or join the “${s.ap.ssid}” hotspot.`,
        duration: 10000,
      });
      setPassword("");
    } catch (e) {
      toast.error("Could not apply Wi-Fi settings", { description: errorMessage(e) });
    }
  };

  return (
    <div className="space-y-6">
      <PageHeader title="Network" description={`${st.hostname}.local · ${st.mac}`} />

      <Section title="Wi-Fi" actions={<StatusDot status={st.wifi.connected ? "ok" : st.wifi.configured ? "error" : "off"} label={st.wifi.connected ? `Connected · ${st.wifi.ip} · ${st.wifi.rssi} dBm` : st.wifi.configured ? "Not connected" : "Not configured"} />}>
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label="Network name (SSID)" htmlFor="ssid">
            <div className="flex gap-2">
              <Input id="ssid" value={ssid} onChange={(e) => setSsid(e.target.value)} maxLength={32} />
              <Button variant="outline" onClick={() => void scan()} disabled={scanning} aria-label="Scan for networks"><Radar className="size-4" /></Button>
            </div>
          </Field>
          <Field label="Password" htmlFor="wifi-pass" hint={s.wifi.hasPassword ? "•••••• (set) — leave empty to keep" : "8–63 characters"}>
            <Input id="wifi-pass" type="password" autoComplete="new-password" value={password} onChange={(e) => setPassword(e.target.value)} maxLength={63} />
          </Field>
        </div>
        {networks && (
          <div className="flex flex-wrap gap-2">
            {networks.map((n) => (
              <button key={n.ssid + n.channel} type="button" onClick={() => setSsid(n.ssid)} className="inline-flex min-h-11 items-center gap-2 rounded-full border px-3 text-sm hover:border-primary">
                <Wifi className="size-4" aria-hidden="true" /> {n.ssid} <span className="text-xs text-muted-foreground">{n.rssi} dBm</span>
              </button>
            ))}
          </div>
        )}
        <label className="flex items-center gap-3 text-sm">
          <Switch checked={staticIp.enabled} onCheckedChange={(enabled) => setStaticIp({ ...staticIp, enabled })} aria-label="Use a static IP address" />
          Static IP address
        </label>
        {staticIp.enabled && (
          <div className="grid gap-3 sm:grid-cols-4">
            {(["ip", "netmask", "gateway", "dns"] as const).map((k) => (
              <Field key={k} label={k === "ip" ? "IP address" : k === "dns" ? "DNS" : k[0].toUpperCase() + k.slice(1)} htmlFor={`st-${k}`}>
                <Input id={`st-${k}`} inputMode="decimal" value={staticIp[k]} onChange={(e) => setStaticIp({ ...staticIp, [k]: e.target.value })} />
              </Field>
            ))}
          </div>
        )}
        <Button onClick={() => setConfirmWifi(true)} disabled={!ssid && !s.wifi.ssid}>Save Wi-Fi</Button>
      </Section>

      <Section title="Setup hotspot" actions={<StatusDot status={st.ap.active ? "ok" : "off"} label={st.ap.active ? `On · ${st.ap.clients} connected` : "Off"} />}>
        <p className="text-sm text-muted-foreground">
          The hotspot <span className="font-medium text-foreground">{s.ap.ssid}</span> is always WPA2-protected. Use it to reach the controller when Wi-Fi is unavailable (address 192.168.4.1).
        </p>
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label="When to run the hotspot">
            <Select value={policy} onValueChange={(v) => setPolicy(v as typeof policy)}>
              <SelectTrigger aria-label="Hotspot policy"><SelectValue /></SelectTrigger>
              <SelectContent>{Object.entries(POLICIES).map(([k, v]) => <SelectItem key={k} value={k}>{v}</SelectItem>)}</SelectContent>
            </Select>
          </Field>
          <Field label="New hotspot password" htmlFor="ap-pass" hint="Leave empty to keep the current one">
            <Input id="ap-pass" type="password" autoComplete="new-password" value={apPassword} onChange={(e) => setApPassword(e.target.value)} maxLength={63} />
          </Field>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          <Button
            onClick={async () => {
              try {
                await api.updateAp({ policy, ...(apPassword ? { password: apPassword } : {}) });
                toast.success("Hotspot settings saved", { duration: 2500 });
                setApPassword("");
              } catch (e) {
                toast.error("Could not save", { description: errorMessage(e) });
              }
            }}
          >
            Save hotspot
          </Button>
          <Button variant="outline" onClick={async () => setShownApPassword((await api.apPassword()).password)}><Eye className="size-4" /> Show password</Button>
          {shownApPassword && <span className="font-mono text-sm">{shownApPassword}</span>}
        </div>
      </Section>

      {s.ethernet.present && (
        <Section title="Ethernet" actions={<StatusDot status={st.ethernet.link ? "ok" : "off"} label={st.ethernet.link ? `Link up · ${st.ethernet.ip}` : "No link"} />}>
          <label className="flex items-center gap-3 text-sm">
            <Switch
              checked={s.ethernet.enabled}
              aria-label="Enable Ethernet"
              onCheckedChange={async (enabled) => {
                try {
                  await api.updateEthernet(enabled);
                  refetch();
                  toast.success(`Ethernet ${enabled ? "enabled" : "disabled"} — restart the controller to apply`, { duration: 5000 });
                } catch (e) {
                  toast.error("Could not save", { description: errorMessage(e) });
                }
              }}
            />
            Use the wired Ethernet port (applies after restart)
          </label>
          <p className="text-xs text-muted-foreground">On the ESP32 the Ethernet clock is generated on GPIO17; running Wi-Fi at the same time can make it unstable (ESP32 errata), so prefer one connection.</p>
        </Section>
      )}

      <Section title="Name and time" actions={<StatusDot status={st.timeSynced ? "ok" : "warn"} label={st.timeSynced ? "Clock synchronised" : "Clock not synchronised"} />}>
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label="Hostname" htmlFor="hostname" hint={`Reachable at http://${hostname || s.hostname}.local/`}>
            <Input id="hostname" value={hostname} onChange={(e) => setHostname(e.target.value.toLowerCase())} maxLength={31} />
          </Field>
          <Field label="Time zone (POSIX TZ)" htmlFor="tz" hint="e.g. IST-5:30 for Sri Lanka, CET-1CEST,M3.5.0,M10.5.0/3 for Berlin">
            <Input id="tz" value={timezone} onChange={(e) => setTimezone(e.target.value)} maxLength={48} />
          </Field>
        </div>
        <Button
          onClick={async () => {
            try {
              await api.updateNetworkSystem({ hostname, timezone });
              refetch();
              toast.success("Saved", { duration: 2500 });
            } catch (e) {
              toast.error("Could not save", { description: errorMessage(e) });
            }
          }}
        >
          Save
        </Button>
      </Section>

      <ConfirmDialog
        open={confirmWifi}
        onOpenChange={setConfirmWifi}
        title="Change Wi-Fi settings?"
        description={`The controller will reconnect and this page may lose its connection. If it does not come back, join the “${s.ap.ssid}” hotspot and open http://192.168.4.1/.`}
        confirmLabel="Apply and reconnect"
        onConfirm={() => void applyWifi()}
      />
    </div>
  );
}
