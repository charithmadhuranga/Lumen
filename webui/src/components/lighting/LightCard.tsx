// LightCard (DESIGN §5.2): icon, name, power, brightness; expands for the
// type-specific controls. Values shown are the requested target state
// (WebSocket-confirmed); slider drags update locally and are throttled.
import * as React from "react";
import { ChevronDown, Lightbulb, TriangleAlert } from "lucide-react";

import { BrightnessSlider, CctSlider, ColorPicker, EffectPicker, PowerToggle } from "@/components/lighting/controls";
import { useEffects, useLightCommand } from "@/services/queries";
import type { Light, LightCommand } from "@/schemas";
import { cn } from "@/lib/utils";

const TYPE_LABEL: Record<Light["type"], string> = {
  switch: "Switch",
  dimmer: "Dimmer",
  rgb: "RGB",
  rgbw: "RGBW",
  cct: "Tunable white",
  addressable_rgb: "Addressable RGB",
  addressable_rgbw: "Addressable RGBW",
};

export function lightGlow(l: Light): string | undefined {
  if (!l.state.power) return undefined;
  if (l.type === "rgb" || l.type === "rgbw" || l.type.startsWith("addressable")) {
    return `rgb(${l.state.red} ${l.state.green} ${l.state.blue})`;
  }
  return "var(--light-on-glow)";
}

export function LightCard({ light, compact = false }: { light: Light; compact?: boolean }) {
  const command = useLightCommand();
  const effects = useEffects();
  const [open, setOpen] = React.useState(false);
  const send = React.useCallback(
    (c: LightCommand) => command.mutate({ id: light.id, command: c }),
    [command, light.id],
  );
  const s = light.state;
  const caps = {
    brightness: light.type !== "switch",
    color: ["rgb", "rgbw", "addressable_rgb", "addressable_rgbw"].includes(light.type),
    white: ["rgbw", "addressable_rgbw"].includes(light.type),
    cct: light.type === "cct",
    effects: light.type !== "switch",
  };
  const hasMore = caps.color || caps.white || caps.cct || caps.effects;
  const glow = lightGlow(light);

  return (
    <div
      className={cn(
        "rounded-xl border bg-card p-4 transition-shadow",
        s.power && "shadow-[0_0_24px_-12px_var(--glow)]",
        !light.available && "border-warning/50",
      )}
      style={glow ? ({ "--glow": glow } as React.CSSProperties) : undefined}
    >
      <div className="flex items-center gap-3">
        <PowerToggle on={s.power} label={light.name} onToggle={() => send({ toggle: true })} />
        <div className="min-w-0 flex-1">
          <p className="truncate font-medium">{light.name}</p>
          <p className="flex items-center gap-1 text-xs text-muted-foreground">
            {!light.available ? (
              <>
                <TriangleAlert className="size-3 text-warning" aria-hidden="true" /> Hardware unavailable
              </>
            ) : (
              <>
                {TYPE_LABEL[light.type]}
                {s.power && caps.brightness ? ` · ${s.brightness}%` : s.power ? " · On" : " · Off"}
                {light.effect ? ` · ${light.effect.replace("_", " ")}` : ""}
              </>
            )}
          </p>
        </div>
        <Lightbulb
          className={cn("size-5", s.power ? "text-primary" : "text-muted-foreground")}
          style={glow ? { color: glow } : undefined}
          aria-hidden="true"
        />
        {hasMore && !compact && (
          <button
            type="button"
            className="rounded-md p-2 text-muted-foreground hover:bg-muted focus-visible:outline-none focus-visible:ring-3 focus-visible:ring-ring/50"
            aria-expanded={open}
            aria-label={`${open ? "Hide" : "Show"} controls for ${light.name}`}
            onClick={() => setOpen((o) => !o)}
          >
            <ChevronDown className={cn("size-4 transition-transform", open && "rotate-180")} />
          </button>
        )}
      </div>
      {caps.brightness && (
        <BrightnessSlider
          value={s.power ? s.brightness : 0}
          label={`${light.name} brightness`}
          onLive={(v) => send({ brightness: v, transitionMs: 0 })}
          onCommit={(v) => send({ brightness: v, transitionMs: 150 })}
        />
      )}
      {open && (
        <div className="mt-2 space-y-4 border-t pt-4">
          {caps.color && (
            <ColorPicker
              value={{ r: s.red, g: s.green, b: s.blue }}
              onLive={(c) => send({ red: c.r, green: c.g, blue: c.b, transitionMs: 0 })}
              onCommit={(c) => send({ red: c.r, green: c.g, blue: c.b, transitionMs: 200 })}
            />
          )}
          {caps.white && (
            <div>
              <p className="mb-1 text-xs text-muted-foreground">White channel</p>
              <BrightnessSlider
                value={Math.round((s.white / 255) * 100)}
                label={`${light.name} white`}
                onLive={(v) => send({ white: Math.round((v / 100) * 255), transitionMs: 0 })}
                onCommit={(v) => send({ white: Math.round((v / 100) * 255), transitionMs: 150 })}
              />
            </div>
          )}
          {caps.cct && (
            <CctSlider
              value={s.colorTemperature}
              min={light.limits.colorTemperatureMin}
              max={light.limits.colorTemperatureMax}
              onLive={(k) => send({ colorTemperature: k, transitionMs: 0 })}
              onCommit={(k) => send({ colorTemperature: k, transitionMs: 200 })}
            />
          )}
          {caps.effects && (
            <div>
              <p className="mb-1 text-xs text-muted-foreground">Effect</p>
              <EffectPicker
                effects={effects.data ?? []}
                value={light.effect ?? ""}
                onChange={(effect, speedMs) => send({ effect, effectParams: { speedMs } })}
              />
            </div>
          )}
        </div>
      )}
    </div>
  );
}
