// RoomCard (DESIGN §5.2): name, master power, brightness, light count.
// Aggregates are computed from the live light cache so they update with
// every WebSocket event.
import { Link } from "react-router-dom";
import { Home } from "lucide-react";

import { BrightnessSlider, PowerToggle } from "@/components/lighting/controls";
import { useRoomBrightness, useRoomPower } from "@/services/queries";
import type { Light, Room } from "@/schemas";
import { cn } from "@/lib/utils";

export function roomStats(room: Room, lights: Light[]) {
  const members = lights.filter((l) => room.channelIds.includes(l.id));
  const on = members.filter((l) => l.state.power);
  const dimmable = on.filter((l) => l.type !== "switch");
  const brightness = dimmable.length ? Math.round(dimmable.reduce((a, l) => a + l.state.brightness, 0) / dimmable.length) : on.length ? 100 : 0;
  return { members, onCount: on.length, brightness };
}

export function RoomCard({ room, lights }: { room: Room; lights: Light[] }) {
  const power = useRoomPower();
  const bright = useRoomBrightness();
  const { members, onCount, brightness } = roomStats(room, lights);
  const anyOn = onCount > 0;
  const hasDimmer = members.some((l) => l.type !== "switch");
  return (
    <div className={cn("rounded-xl border bg-card p-4", anyOn && "border-primary/40")}>
      <div className="flex items-center gap-3">
        <PowerToggle
          on={anyOn}
          label={room.name}
          disabled={members.length === 0}
          onToggle={() => power.mutate({ id: room.id, power: !anyOn })}
        />
        <Link to={`/rooms/${room.id}`} className="min-w-0 flex-1 rounded-md focus-visible:outline-none focus-visible:ring-3 focus-visible:ring-ring/50">
          <p className="truncate font-medium">{room.name}</p>
          <p className="text-xs text-muted-foreground">
            {members.length === 0 ? "No lights yet" : `${onCount} of ${members.length} on`}
            {room.floor ? ` · ${room.floor}` : ""}
          </p>
        </Link>
        <Home className="size-5 text-muted-foreground" aria-hidden="true" />
      </div>
      {hasDimmer && (
        <BrightnessSlider
          value={anyOn ? brightness : 0}
          label={`${room.name} brightness`}
          onLive={(v) => bright.mutate({ id: room.id, brightness: v })}
          onCommit={(v) => bright.mutate({ id: room.id, brightness: v })}
        />
      )}
    </div>
  );
}
