"use client";

import * as React from "react";
import { cn } from "@/lib/utils";
import { AlertTriangle, Lock } from "lucide-react";
import type { BoardPin } from "@/schemas";

const dirBadge: Record<BoardPin["dir"], string> = {
  input: "IN",
  output: "OUT",
  bidirectional: "I/O",
  "input-only": "IN-ONLY",
  free: "FREE",
};

export interface PinChipProps {
  pin: BoardPin;
  onAssign?: (gpio: number) => void;
  selected?: boolean;
}

/**
 * A single GPIO pin in the Pin Mapping grid (mockup-ui §7.8 `.pin-chip`).
 * Colored by role/state with conflict, strapping and input-only indicators.
 * All pin data comes from the API — no GPIO knowledge lives in the frontend.
 */
export function PinChip({ pin, onAssign, selected }: PinChipProps) {
  const conflict = pin.conflict;

  return (
    <button
      type="button"
      onClick={() => onAssign?.(pin.gpio)}
      data-conflict={conflict || undefined}
      aria-label={`GPIO ${pin.gpio}, ${pin.role}${conflict ? ", conflict" : ""}`}
      className={cn(
        "flex flex-col gap-1 rounded-md border p-3 text-left transition-colors",
        "border-border bg-card hover:border-primary",
        "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring",
        conflict && "border-destructive bg-destructive/10 hover:border-destructive",
        selected && "border-primary ring-2 ring-ring"
      )}
    >
      <div className="flex items-center justify-between gap-2">
        <span className="font-mono text-[13px] font-bold">{pin.gpio >= 0 ? `GPIO ${pin.gpio}` : "—"}</span>
        <span
          className={cn(
            "rounded px-1.5 py-0.5 text-[10px] font-medium",
            pin.dir === "free"
              ? "bg-muted text-muted-foreground"
              : pin.dir === "input-only"
                ? "bg-info/15 text-info"
                : "bg-secondary text-secondary-foreground"
          )}
        >
          {dirBadge[pin.dir]}
        </span>
      </div>

      <span className="truncate text-xs text-muted-foreground" title={pin.role}>
        {pin.role}
      </span>

      <div className="flex flex-wrap items-center gap-1.5">
        {pin.strapping && (
          <span className="inline-flex items-center gap-1 rounded bg-warning/15 px-1.5 py-0.5 text-[10px] font-medium text-warning">
            <Lock className="h-3 w-3" /> strapping
          </span>
        )}
        {pin.input_only && (
          <span className="inline-flex items-center gap-1 rounded bg-info/15 px-1.5 py-0.5 text-[10px] font-medium text-info">
            input-only
          </span>
        )}
        {conflict && (
          <span className="inline-flex items-center gap-1 rounded bg-destructive/15 px-1.5 py-0.5 text-[10px] font-medium text-destructive">
            <AlertTriangle className="h-3 w-3" /> conflict
          </span>
        )}
      </div>

      {conflict && pin.reason && (
        <span className="text-[11px] leading-snug text-destructive">{pin.reason}</span>
      )}
    </button>
  );
}