// Overview (DESIGN §7.2): connectivity, lights-on summary with "All off",
// rooms grid, scenes row, alerts. No engineering metrics here.
import { Link } from "react-router-dom";
import { Lightbulb, PowerOff, Sparkles, TriangleAlert } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { EmptyState, PageHeader, StatusDot } from "@/components/common";
import { RoomCard } from "@/components/lighting/RoomCard";
import { LightCard } from "@/components/lighting/LightCard";
import { useActivateScene, useHardwareStatus, useLightCommand, useLights, useRooms, useScenes, useSystemInfo } from "@/services/queries";
import { useConnectionStatus } from "@/stores/liveStore";

export default function Overview() {
  const lights = useLights();
  const rooms = useRooms();
  const scenes = useScenes();
  const info = useSystemInfo();
  const hw = useHardwareStatus();
  const command = useLightCommand();
  const activate = useActivateScene();
  const status = useConnectionStatus();

  const all = lights.data ?? [];
  const on = all.filter((l) => l.state.power);
  const unassigned = all.filter((l) => !(rooms.data ?? []).some((r) => r.channelIds.includes(l.id)));
  const alerts = [
    ...all.filter((l) => !l.available).map((l) => `“${l.name}” has no working hardware output`),
    ...(hw.data?.peripherals ?? []).filter((p) => !p.ok).map((p) => `${p.id} (${p.type}) is not responding`),
  ];

  return (
    <div className="space-y-8">
      <PageHeader
        title={info.data ? `Hello — ${info.data.hostname}` : "Overview"}
        description={undefined}
        actions={<StatusDot status={status === "connected" ? "ok" : status === "offline" ? "error" : "warn"} label={status === "connected" ? "Online" : "Reconnecting"} />}
      />

      <section aria-labelledby="summary" className="flex flex-wrap items-center justify-between gap-3 rounded-xl border bg-card p-4">
        <div className="flex items-center gap-3">
          <Lightbulb className="size-6 text-primary" aria-hidden="true" />
          <p id="summary" className="text-lg font-medium">
            {lights.isLoading ? "Loading lights…" : `${on.length} of ${all.length} lights on`}
          </p>
        </div>
        <Button variant="outline" size="lg" disabled={on.length === 0} onClick={() => on.forEach((l) => command.mutate({ id: l.id, command: { power: false } }))}>
          <PowerOff className="size-4" /> All off
        </Button>
      </section>

      {alerts.length > 0 && (
        <section aria-label="Alerts" className="space-y-2">
          {alerts.map((a) => (
            <p key={a} className="flex items-center gap-2 rounded-lg border border-warning/40 bg-warning/10 px-3 py-2 text-sm">
              <TriangleAlert className="size-4 shrink-0 text-warning" aria-hidden="true" /> {a}
            </p>
          ))}
        </section>
      )}

      <section aria-labelledby="rooms-h" className="space-y-3">
        <div className="flex items-center justify-between">
          <h2 id="rooms-h" className="text-lg font-semibold">Rooms</h2>
          <Link className="text-sm text-link hover:underline" to="/rooms">Manage</Link>
        </div>
        {rooms.isLoading ? (
          <div className="grid items-start gap-3 sm:grid-cols-2 xl:grid-cols-3">{[0, 1, 2].map((i) => <Skeleton key={i} className="h-28 rounded-xl" />)}</div>
        ) : (rooms.data ?? []).length === 0 ? (
          <EmptyState icon={Lightbulb} title="No rooms yet" description="Group your lights into rooms to control them together." action={<Button asChild><Link to="/rooms">Create a room</Link></Button>} />
        ) : (
          <div className="grid items-start gap-3 sm:grid-cols-2 xl:grid-cols-3">
            {(rooms.data ?? []).map((r) => <RoomCard key={r.id} room={r} lights={all} />)}
          </div>
        )}
      </section>

      <section aria-labelledby="scenes-h" className="space-y-3">
        <div className="flex items-center justify-between">
          <h2 id="scenes-h" className="text-lg font-semibold">Scenes</h2>
          <Link className="text-sm text-link hover:underline" to="/scenes">All scenes</Link>
        </div>
        {(scenes.data ?? []).length === 0 ? (
          <p className="text-sm text-muted-foreground">Save the current lighting as a scene from the Scenes page.</p>
        ) : (
          <div className="-mx-4 flex snap-x gap-3 overflow-x-auto px-4 pb-2">
            {(scenes.data ?? []).map((s) => (
              <button
                key={s.id}
                type="button"
                onClick={() => activate.mutate(s.id)}
                className="flex min-h-11 shrink-0 snap-start items-center gap-2 rounded-full border bg-card px-4 text-sm font-medium hover:border-primary focus-visible:outline-none focus-visible:ring-3 focus-visible:ring-ring/50"
              >
                <Sparkles className="size-4 text-primary" aria-hidden="true" /> {s.name}
              </button>
            ))}
          </div>
        )}
      </section>

      {unassigned.length > 0 && (
        <section aria-labelledby="unassigned-h" className="space-y-3">
          <h2 id="unassigned-h" className="text-lg font-semibold">Other lights</h2>
          <div className="grid items-start gap-3 sm:grid-cols-2 xl:grid-cols-3">
            {unassigned.map((l) => <LightCard key={l.id} light={l} />)}
          </div>
        </section>
      )}
    </div>
  );
}
