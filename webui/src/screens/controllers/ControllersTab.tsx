// Devices → Controllers (DESIGN §7.7): other Lumen controllers on the same
// ESP-NOW network, their status and lights. Remote lights show the state
// their owner reports; switching one sends a command to its owner.
import { useMutation, useQuery } from "@tanstack/react-query";
import { Network } from "lucide-react";
import { Link } from "react-router-dom";
import { toast } from "sonner";

import { Badge } from "@/components/ui/badge";
import { EmptyState, StatusDot } from "@/components/common";
import { errorMessage } from "@/hooks/useAuth";
import { controllersApi, controllersKey, type PeerT } from "./api";

function signal(rssi: number) {
  return rssi >= -60 ? "strong" : rssi >= -75 ? "good" : "weak";
}

export function ControllersTab() {
  const q = useQuery({ queryKey: controllersKey, queryFn: controllersApi.get, refetchInterval: 5000 });
  const cmd = useMutation({
    mutationFn: ({ peer, light, power }: { peer: string; light: string; power: boolean }) => controllersApi.command(peer, light, { power }),
    onError: (e) => toast.error("Command not sent", { description: errorMessage(e) }),
  });
  if (!q.data) return <p className="text-sm text-muted-foreground">{q.error ? errorMessage(q.error) : "Loading…"}</p>;
  const { self, peers } = q.data;
  if (!self.enabled) {
    return (
      <EmptyState
        icon={Network}
        title="Multi-controller is off"
        description="Link several Lumen controllers over ESP-NOW so they can see and switch each other's lights."
        action={<Link className="text-sm text-link underline" to="/integrations">Set it up in Integrations</Link>}
      />
    );
  }
  return (
    <div className="space-y-3">
      <p className="text-sm text-muted-foreground">
        This controller: <span className="font-medium text-foreground">{self.id}</span> · network “{self.networkId}” · {peers.length}/{self.maxPeers} peers
      </p>
      {peers.length === 0 && <EmptyState icon={Network} title="No other controllers yet" description="Give each controller the same network id and key under Integrations → Multi-controller." />}
      {peers.map((p: PeerT) => (
        <section key={p.id} className="space-y-3 rounded-xl border bg-card p-4" aria-label={`Controller ${p.id}`}>
          <div className="flex flex-wrap items-center justify-between gap-2">
            <div>
              <h3 className="font-semibold">{p.id}</h3>
              <p className="text-xs text-muted-foreground">{p.ip || "no IP"} · firmware {p.version || "?"} · signal {signal(p.rssi)} ({p.rssi} dBm)</p>
            </div>
            <StatusDot status={p.online ? "ok" : "off"} label={p.online ? "Online" : `Offline · last seen ${p.lastSeenSec} s ago`} />
          </div>
          {p.lights.length === 0 ? (
            <p className="text-sm text-muted-foreground">No lights reported yet.</p>
          ) : (
            <ul className="grid gap-2 sm:grid-cols-2">
              {p.lights.map((l) => (
                <li key={l.id}>
                  <button
                    type="button"
                    role="switch"
                    aria-checked={l.output.power}
                    disabled={!p.online || cmd.isPending}
                    onClick={() => cmd.mutate({ peer: p.id, light: l.id, power: !l.output.power })}
                    className="flex min-h-11 w-full items-center justify-between rounded-lg border px-3 text-left text-sm hover:border-primary disabled:opacity-50"
                  >
                    <span>{l.name}</span>
                    <span className="flex items-center gap-2 text-muted-foreground">
                      {l.type !== "switch" && l.output.power && `${l.output.brightness} %`}
                      <Badge variant={l.output.power ? "default" : "secondary"}>{l.output.power ? "On" : "Off"}</Badge>
                    </span>
                  </button>
                </li>
              ))}
            </ul>
          )}
        </section>
      ))}
    </div>
  );
}
