// One GPIO in the Pin Mapping grid (DESIGN §7.8). Coloured by role with
// conflict, strapping, input-only and reserved indicators. All pin data
// comes from the firmware — no GPIO knowledge is compiled into the UI.
import { AlertTriangle, Lock } from "lucide-react";

import type { PinInfo } from "@/schemas";
import { cn } from "@/lib/utils";

const DIR: Record<string, string> = {
  input: "IN",
  output: "OUT",
  bidirectional: "I/O",
  "input-only": "IN-ONLY",
  free: "FREE",
};

export function PinChip({ pin }: { pin: PinInfo }) {
  const used = pin.owner !== "";
  return (
    <div
      role="listitem"
      aria-label={`GPIO ${pin.gpio}: ${used ? pin.owner : pin.reserved ? "reserved for flash" : "free"}${pin.conflict ? ", conflict" : ""}${pin.strapping ? ", strapping pin" : ""}`}
      title={pin.reason ?? (pin.strapping ? "Boot-strapping pin: assigning it may affect boot" : undefined)}
      className={cn(
        "flex min-h-16 flex-col justify-between gap-1 rounded-lg border p-2 text-left",
        used ? "bg-primary/10 border-primary/40" : "bg-card",
        pin.reserved && "bg-muted text-muted-foreground",
        pin.conflict && "border-destructive bg-destructive/10",
      )}
    >
      <div className="flex items-center justify-between gap-1">
        <span className="font-mono text-xs font-semibold">GPIO{pin.gpio}</span>
        <span className="rounded bg-muted px-1 text-[10px] font-medium text-muted-foreground">{DIR[pin.dir] ?? pin.dir.toUpperCase()}</span>
      </div>
      <span className="truncate text-[11px]" title={pin.owner || undefined}>
        {used ? pin.owner : pin.reserved ? pin.role : "—"}
      </span>
      <div className="flex gap-1">
        {pin.conflict && <AlertTriangle className="size-3 text-destructive" aria-hidden="true" />}
        {pin.strapping && <span className="text-[10px] text-warning">strap</span>}
        {pin.reserved && <Lock className="size-3" aria-hidden="true" />}
      </div>
    </div>
  );
}
