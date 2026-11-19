"use client";

import * as React from "react";
import { cn } from "@/lib/utils";
import { WifiOff, Wifi } from "lucide-react";
import { useConnectionStatus } from "@/stores/liveStore";

export function ConnectionBanner() {
  const status = useConnectionStatus();

  if (status === "connected") return null;

  return (
    <div
      className={cn(
        "fixed top-0 left-0 right-0 z-50 px-4 py-2 flex items-center justify-center gap-2 text-xs font-medium",
        status === "failed"
          ? "bg-destructive text-destructive-foreground border-b border-destructive/30"
          : "bg-warning text-warning-foreground border-b border-warning/30"
      )}
    >
      {status === "failed" ? (
        <WifiOff className="h-3 w-3" />
      ) : (
        <Wifi className="h-3 w-3 animate-pulse" />
      )}
      <span>
        {status === "failed"
          ? "Disconnected from controller"
          : "Connecting to controller..."}
      </span>
    </div>
  );
}