// Lighting controls (DESIGN §5.2–5.3). Every control is type-aware: a light
// only ever shows controls for properties it supports.
import * as React from "react";
import { Power } from "lucide-react";

import { Slider } from "@/components/ui/slider";
import { Input } from "@/components/ui/input";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { useThrottled } from "@/hooks/useThrottledCommand";
import { hexToRgb, hsvToRgb, kelvinToCss, rgbToHex, rgbToHsv, type Rgb } from "@/lib/color";
import { cn } from "@/lib/utils";

/* ------------------------------------------------------------- power */
export function PowerToggle({
  on,
  onToggle,
  label,
  disabled,
  size = "md",
}: {
  on: boolean;
  onToggle: () => void;
  label: string;
  disabled?: boolean;
  size?: "md" | "lg";
}) {
  return (
    <button
      type="button"
      onClick={onToggle}
      disabled={disabled}
      aria-pressed={on}
      aria-label={`${label}: ${on ? "on" : "off"}. Toggle`}
      className={cn(
        "inline-flex shrink-0 items-center justify-center rounded-full border transition-colors focus-visible:outline-none focus-visible:ring-3 focus-visible:ring-ring/50 disabled:opacity-50",
        size === "lg" ? "size-12" : "size-11",
        on
          ? "border-transparent bg-primary text-primary-foreground shadow-[0_0_16px_-2px_var(--light-on-glow)]"
          : "bg-muted text-muted-foreground hover:bg-accent",
      )}
    >
      <Power className="size-5" aria-hidden="true" />
    </button>
  );
}

/* -------------------------------------------------------- brightness */
export function BrightnessSlider({
  value,
  onLive,
  onCommit,
  disabled,
  label = "Brightness",
}: {
  value: number;
  onLive: (v: number) => void;
  onCommit: (v: number) => void;
  disabled?: boolean;
  label?: string;
}) {
  const [local, setLocal] = React.useState(value);
  const dragging = React.useRef(false);
  React.useEffect(() => {
    if (!dragging.current) setLocal(value);
  }, [value]);
  const { push, commit } = useThrottled(onLive, 100);
  return (
    <div className="flex items-center gap-3 py-2">
      <Slider
        value={[local]}
        min={1}
        max={100}
        step={1}
        disabled={disabled}
        aria-label={label}
        className="py-3"
        onValueChange={([v]) => {
          dragging.current = true;
          setLocal(v);
          push(v);
        }}
        onValueCommit={([v]) => {
          dragging.current = false;
          commit(v);
          onCommit(v);
        }}
      />
      <span className="w-10 text-right text-sm tabular-nums text-muted-foreground">{local}%</span>
    </div>
  );
}

/* ------------------------------------------------------------- CCT */
export function CctSlider({
  value,
  min = 2700,
  max = 6500,
  onLive,
  onCommit,
  disabled,
}: {
  value: number;
  min?: number;
  max?: number;
  onLive: (k: number) => void;
  onCommit: (k: number) => void;
  disabled?: boolean;
}) {
  const [local, setLocal] = React.useState(value || min);
  const dragging = React.useRef(false);
  React.useEffect(() => {
    if (!dragging.current) setLocal(value || min);
  }, [value, min]);
  const { push, commit } = useThrottled(onLive, 100);
  return (
    <div className="space-y-1">
      <div className="relative py-2">
        <div
          className="pointer-events-none absolute inset-x-0 top-1/2 h-2 -translate-y-1/2 rounded-full"
          style={{ background: `linear-gradient(to right, ${kelvinToCss(min)}, ${kelvinToCss(max)})` }}
          aria-hidden="true"
        />
        <Slider
          value={[local]}
          min={min}
          max={max}
          step={50}
          disabled={disabled}
          aria-label="Colour temperature"
          className="py-3 [&_[data-orientation=horizontal]]:bg-transparent [&_.bg-primary]:bg-transparent"
          onValueChange={([v]) => {
            dragging.current = true;
            setLocal(v);
            push(v);
          }}
          onValueCommit={([v]) => {
            dragging.current = false;
            commit(v);
            onCommit(v);
          }}
        />
      </div>
      <div className="flex justify-between text-xs text-muted-foreground">
        <span>Warm</span>
        <span className="tabular-nums">{local} K</span>
        <span>Cool</span>
      </div>
    </div>
  );
}

/* ----------------------------------------------------------- colour */
export function ColorPicker({
  value,
  onLive,
  onCommit,
  disabled,
}: {
  value: Rgb;
  onLive: (c: Rgb) => void;
  onCommit: (c: Rgb) => void;
  disabled?: boolean;
}) {
  const [hsv, setHsv] = React.useState(() => rgbToHsv(value));
  const [hex, setHex] = React.useState(rgbToHex(value));
  const dragging = React.useRef(false);
  const area = React.useRef<HTMLDivElement>(null);
  const { push, commit } = useThrottled(onLive, 100);

  React.useEffect(() => {
    if (!dragging.current) {
      const next = rgbToHsv(value);
      setHsv((h) => (next.s === 0 || next.v === 0 ? { ...next, h: h.h } : next));
      setHex(rgbToHex(value));
    }
  }, [value]);

  const fromPointer = (e: React.PointerEvent<HTMLDivElement>) => {
    const rect = area.current?.getBoundingClientRect();
    if (!rect) return hsv;
    const s = Math.min(Math.max((e.clientX - rect.left) / rect.width, 0), 1);
    const v = 1 - Math.min(Math.max((e.clientY - rect.top) / rect.height, 0), 1);
    return { ...hsv, s, v: Math.max(v, 0.05) };
  };
  const update = (next: typeof hsv, final: boolean) => {
    setHsv(next);
    const rgb = hsvToRgb(next);
    setHex(rgbToHex(rgb));
    if (final) {
      commit(rgb);
      onCommit(rgb);
    } else {
      push(rgb);
    }
  };
  const hueColor = rgbToHex(hsvToRgb({ h: hsv.h, s: 1, v: 1 }));

  return (
    <div className={cn("space-y-3", disabled && "pointer-events-none opacity-50")}>
      <div
        ref={area}
        role="slider"
        tabIndex={0}
        aria-label="Saturation and brightness"
        aria-valuetext={hex}
        className="relative h-36 w-full cursor-crosshair touch-none rounded-lg"
        style={{
          background: `linear-gradient(to top, #000, transparent), linear-gradient(to right, #fff, ${hueColor})`,
        }}
        onPointerDown={(e) => {
          dragging.current = true;
          e.currentTarget.setPointerCapture(e.pointerId);
          update(fromPointer(e), false);
        }}
        onPointerMove={(e) => {
          if (dragging.current) update(fromPointer(e), false);
        }}
        onPointerUp={(e) => {
          dragging.current = false;
          update(fromPointer(e), true);
        }}
        onKeyDown={(e) => {
          const step = 0.05;
          const next = { ...hsv };
          if (e.key === "ArrowRight") next.s = Math.min(1, hsv.s + step);
          else if (e.key === "ArrowLeft") next.s = Math.max(0, hsv.s - step);
          else if (e.key === "ArrowUp") next.v = Math.min(1, hsv.v + step);
          else if (e.key === "ArrowDown") next.v = Math.max(0.05, hsv.v - step);
          else return;
          e.preventDefault();
          update(next, true);
        }}
      >
        <span
          className="pointer-events-none absolute size-4 -translate-x-1/2 translate-y-1/2 rounded-full border-2 border-white shadow"
          style={{ left: `${hsv.s * 100}%`, bottom: `${hsv.v * 100}%`, background: hex }}
          aria-hidden="true"
        />
      </div>
      <div className="relative py-2">
        <div
          className="pointer-events-none absolute inset-x-0 top-1/2 h-2 -translate-y-1/2 rounded-full"
          style={{ background: "linear-gradient(to right,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)" }}
          aria-hidden="true"
        />
        <Slider
          value={[Math.round(hsv.h)]}
          min={0}
          max={359}
          step={1}
          aria-label="Hue"
          className="py-3 [&_.bg-primary]:bg-transparent [&_[data-orientation=horizontal]]:bg-transparent"
          onValueChange={([h]) => {
            dragging.current = true;
            update({ ...hsv, h, s: hsv.s < 0.05 ? 1 : hsv.s, v: hsv.v < 0.05 ? 1 : hsv.v }, false);
          }}
          onValueCommit={([h]) => {
            dragging.current = false;
            update({ ...hsv, h }, true);
          }}
        />
      </div>
      <div className="flex items-center gap-2">
        <span className="size-8 shrink-0 rounded-md border" style={{ background: hex }} aria-hidden="true" />
        <Input
          aria-label="Hex colour"
          value={hex}
          maxLength={7}
          className="font-mono"
          onChange={(e) => {
            setHex(e.target.value);
            const rgb = hexToRgb(e.target.value);
            if (rgb) {
              setHsv(rgbToHsv(rgb));
              commit(rgb);
              onCommit(rgb);
            }
          }}
        />
      </div>
    </div>
  );
}

/* ----------------------------------------------------------- effects */
const EFFECT_LABELS: Record<string, string> = {
  breathing: "Breathing",
  pulse: "Pulse",
  rainbow: "Rainbow",
  color_cycle: "Colour cycle",
  candlelight: "Candlelight",
  sunrise: "Sunrise",
  sunset: "Sunset",
  strobe: "Strobe",
};

export function EffectPicker({
  effects,
  value,
  onChange,
}: {
  effects: string[];
  value: string;
  onChange: (effect: string, speedMs: number) => void;
}) {
  const [speed, setSpeed] = React.useState(2000);
  return (
    <div className="space-y-2">
      <Select value={value || "none"} onValueChange={(v) => onChange(v === "none" ? "stop" : v, speed)}>
        <SelectTrigger aria-label="Effect">
          <SelectValue placeholder="No effect" />
        </SelectTrigger>
        <SelectContent>
          <SelectItem value="none">No effect</SelectItem>
          {effects.map((e) => (
            <SelectItem key={e} value={e}>
              {EFFECT_LABELS[e] ?? e}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
      {value && (
        <div className="flex items-center gap-3">
          <span className="text-xs text-muted-foreground">Speed</span>
          <Slider
            value={[6000 - speed]}
            min={0}
            max={5800}
            step={100}
            aria-label="Effect speed"
            className="py-3"
            onValueChange={([v]) => setSpeed(6000 - v)}
            onValueCommit={([v]) => onChange(value, 6000 - v)}
          />
        </div>
      )}
    </div>
  );
}
