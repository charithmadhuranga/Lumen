// Control mode (DESIGN §9.3): the floor plan as the lighting interface.
// Inline sanitised SVG + overlay layers in ONE viewBox; glows follow the
// lights' reported output (live cache patched by the WebSocket) — nothing is
// simulated here. Tap a light to toggle it, "Details" for full controls.
import * as React from "react";
import { m, LazyMotion, domAnimation, MotionConfig } from "motion/react";
import { Maximize2, Minus, Plus, RotateCcw } from "lucide-react";

import { Button } from "@/components/ui/button";
import { LightCard } from "@/components/lighting/LightCard";
import { useLightCommand } from "@/services/queries";
import type { Light, Room } from "@/schemas";
import { cn } from "@/lib/utils";
import { SanitizedSvg } from "./SanitizedSvg";
import { clampZoom, zoomAt, type View, type ViewBox } from "./coords";
import { glowFor } from "./glow";
import type { FloorPlanT } from "./api";

const IDENTITY: View = { scale: 1, panX: 0, panY: 0 };

export default function FloorPlanViewer({ plan, svg, lights, rooms }: { plan: FloorPlanT; svg: string | null; lights: Light[]; rooms: Room[] }) {
  const vb = plan.viewBox as ViewBox;
  const svgRef = React.useRef<SVGSVGElement>(null);
  const wrapRef = React.useRef<HTMLDivElement>(null);
  const [view, setView] = React.useState<View>(IDENTITY);
  const [selected, setSelected] = React.useState<string | null>(null);
  const [room, setRoom] = React.useState<string | null>(null);
  const command = useLightCommand();
  const byId = React.useMemo(() => new Map(lights.map((l) => [l.id, l])), [lights]);
  const placed = plan.lights.filter((p) => byId.has(p.lightId));
  const unit = Math.max(vb[2], vb[3]) / 100; // 1 % of the drawing

  const toSvg = (clientX: number, clientY: number) => {
    const el = svgRef.current;
    const ctm = el?.getScreenCTM();
    if (!el || !ctm) return { x: 0, y: 0 };
    const p = new DOMPoint(clientX, clientY).matrixTransform(ctm.inverse());
    return { x: p.x, y: p.y };
  };

  // Wheel zoom around the pointer; drag to pan (pointer events = touch too).
  const drag = React.useRef<{ x: number; y: number; moved: boolean } | null>(null);
  const pointers = React.useRef(new Map<number, { x: number; y: number }>());
  const pinch = React.useRef<number | null>(null);
  React.useEffect(() => {
    const el = svgRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const at = toSvgPoint(el, e.clientX, e.clientY);
      setView((v) => zoomAt(v, e.deltaY < 0 ? 1.15 : 1 / 1.15, at));
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => el.removeEventListener("wheel", onWheel);
  }, []);

  const onPointerDown = (e: React.PointerEvent) => {
    pointers.current.set(e.pointerId, toSvg(e.clientX, e.clientY));
    if (pointers.current.size === 1) drag.current = { ...toSvg(e.clientX, e.clientY), moved: false };
  };
  const onPointerMove = (e: React.PointerEvent) => {
    if (!pointers.current.has(e.pointerId)) return;
    const p = toSvg(e.clientX, e.clientY);
    pointers.current.set(e.pointerId, p);
    if (pointers.current.size === 2) {
      const [a, b] = [...pointers.current.values()];
      const d = Math.hypot(a.x - b.x, a.y - b.y);
      if (pinch.current) setView((v) => zoomAt(v, d / pinch.current!, { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 }));
      pinch.current = d;
      return;
    }
    const start = drag.current;
    if (!start) return;
    const dx = p.x - start.x;
    const dy = p.y - start.y;
    if (!start.moved && Math.hypot(dx, dy) < unit) return;
    if (!start.moved) (e.currentTarget as Element).setPointerCapture(e.pointerId);
    start.moved = true;
    start.x = p.x; // incremental: root-space points are unaffected by the pan
    start.y = p.y;
    setView((v) => ({ ...v, panX: v.panX + dx, panY: v.panY + dy }));
  };
  const onPointerUp = (e: React.PointerEvent) => {
    pointers.current.delete(e.pointerId);
    if (pointers.current.size < 2) pinch.current = null;
    if (pointers.current.size === 0) setTimeout(() => (drag.current = null), 0);
  };
  const wasDrag = () => drag.current?.moved === true;

  const zoomBy = (f: number) => setView((v) => zoomAt(v, f, { x: vb[0] + vb[2] / 2, y: vb[1] + vb[3] / 2 }));
  const toggle = (l: Light) => command.mutate({ id: l.id, command: { toggle: true } });

  const selectedLight = selected ? byId.get(selected) : undefined;
  const selectedRoom = room ? rooms.find((r) => r.id === room) : undefined;
  const transform = `translate(${view.panX} ${view.panY}) scale(${view.scale})`;

  return (
    <LazyMotion features={domAnimation} strict>
      <MotionConfig reducedMotion="user">
        <div className="space-y-4">
          <div ref={wrapRef} className="relative overflow-hidden rounded-xl border bg-card">
            <svg
              ref={svgRef}
              viewBox={vb.join(" ")}
              preserveAspectRatio="xMidYMid meet"
              className="block aspect-[4/3] max-h-[70vh] w-full touch-none select-none"
              role="group"
              aria-label={`Floor plan ${plan.name}`}
              onPointerDown={onPointerDown}
              onPointerMove={onPointerMove}
              onPointerUp={onPointerUp}
              onPointerCancel={onPointerUp}
            >
              <defs>
                {/* One shared blur for every glow (DESIGN §9.3). */}
                <filter id="fp-glow-blur" x="-50%" y="-50%" width="200%" height="200%">
                  <feGaussianBlur stdDeviation={unit * 1.5} />
                </filter>
                {placed.map((p) => {
                  const g = glowFor(byId.get(p.lightId)!, p.color);
                  return (
                    <radialGradient key={p.lightId} id={`fp-g-${p.lightId}`}>
                      <m.stop offset="0" animate={{ stopColor: g.color }} transition={{ duration: 0.25 }} stopOpacity={0.95} />
                      <m.stop offset="1" animate={{ stopColor: g.color }} transition={{ duration: 0.25 }} stopOpacity={0} />
                    </radialGradient>
                  );
                })}
              </defs>
              <g transform={transform}>
                {/* 1. Original drawing */}
                {svg && <SanitizedSvg svg={svg} className="text-foreground" />}
                {/* 2. Rooms */}
                {plan.rooms.map((r) => (
                  <polygon
                    key={r.roomId}
                    points={r.points.map((p) => p.join(",")).join(" ")}
                    className={cn("cursor-pointer fill-primary/0 stroke-primary/0 transition-opacity", room === r.roomId && "fill-primary/10 stroke-primary")}
                    strokeWidth={unit * 0.4}
                    onClick={() => !wasDrag() && setRoom(room === r.roomId ? null : r.roomId)}
                  />
                ))}
                {/* 3. Lighting overlay (glows) */}
                <g filter="url(#fp-glow-blur)" pointerEvents="none">
                  {placed.map((p) => {
                    const g = glowFor(byId.get(p.lightId)!, p.color);
                    return (
                      <m.circle
                        key={p.lightId}
                        cx={p.x}
                        cy={p.y}
                        r={p.radius}
                        fill={`url(#fp-g-${p.lightId})`}
                        style={{ transformBox: "fill-box", transformOrigin: "center" }}
                        initial={false}
                        animate={{ opacity: g.opacity, scale: g.radiusScale }}
                        transition={{ duration: 0.25, ease: "easeOut" }}
                        data-testid={`glow-${p.lightId}`}
                        data-on={g.on}
                      />
                    );
                  })}
                </g>
                {/* 4. Interaction layer */}
                {placed.map((p) => {
                  const l = byId.get(p.lightId)!;
                  const on = l.output.power;
                  return (
                    <m.g
                      key={p.lightId}
                      role="switch"
                      aria-checked={on}
                      aria-label={`${l.name}${l.available ? "" : " (unavailable)"}`}
                      tabIndex={0}
                      className="cursor-pointer outline-none [&:focus-visible>circle:last-child]:stroke-ring"
                      whileHover={{ scale: 1.1 }}
                      whileTap={{ scale: 0.92 }}
                      style={{ transformBox: "fill-box", transformOrigin: "center" }}
                      onClick={(e) => {
                        if (wasDrag()) return;
                        if (e.shiftKey) setSelected(p.lightId);
                        else toggle(l);
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        setSelected(p.lightId);
                      }}
                      onKeyDown={(e) => {
                        if (e.key === "Enter" || e.key === " ") {
                          e.preventDefault();
                          toggle(l);
                        } else if (e.key === "i") setSelected(p.lightId);
                      }}
                    >
                      <circle cx={p.x} cy={p.y} r={unit * 3} className="fill-transparent" />
                      <circle
                        cx={p.x}
                        cy={p.y}
                        r={unit * 1.4}
                        className={cn("stroke-background", on ? "fill-primary" : "fill-muted-foreground", !l.available && "opacity-40")}
                        strokeWidth={unit * 0.4}
                      />
                    </m.g>
                  );
                })}
              </g>
            </svg>
            <div className="absolute right-2 top-2 flex flex-col gap-1">
              <Button size="icon" variant="secondary" aria-label="Zoom in" onClick={() => zoomBy(1.4)} disabled={view.scale >= clampZoom(Infinity)}><Plus className="size-4" /></Button>
              <Button size="icon" variant="secondary" aria-label="Zoom out" onClick={() => zoomBy(1 / 1.4)} disabled={view.scale <= clampZoom(0)}><Minus className="size-4" /></Button>
              <Button size="icon" variant="secondary" aria-label="Reset view" onClick={() => setView(IDENTITY)}><RotateCcw className="size-4" /></Button>
              <Button size="icon" variant="secondary" aria-label="Full screen" onClick={() => void wrapRef.current?.requestFullscreen?.()}><Maximize2 className="size-4" /></Button>
            </div>
          </div>
          <p className="text-xs text-muted-foreground">Tap a light to switch it; long-press (right-click) or press “i” for its controls. Drag to move, pinch or scroll to zoom.</p>

          {selectedLight && (
            <div className="space-y-2">
              <div className="flex items-center justify-between">
                <h2 className="font-semibold">{selectedLight.name}</h2>
                <Button variant="ghost" size="sm" onClick={() => setSelected(null)}>Close</Button>
              </div>
              <LightCard light={selectedLight} />
            </div>
          )}
          {selectedRoom && (
            <div className="flex flex-wrap items-center gap-2 rounded-xl border bg-card p-3">
              <span className="font-medium">{selectedRoom.name}</span>
              <Button size="sm" variant="outline" onClick={() => selectedRoom.channelIds.forEach((id) => command.mutate({ id, command: { power: true } }))}>All on</Button>
              <Button size="sm" variant="outline" onClick={() => selectedRoom.channelIds.forEach((id) => command.mutate({ id, command: { power: false } }))}>All off</Button>
            </div>
          )}

          {/* Equivalent accessible list (same lights, same actions). */}
          <ul className="grid gap-2 sm:grid-cols-2" aria-label="Lights on this floor plan">
            {placed.map((p) => {
              const l = byId.get(p.lightId)!;
              return (
                <li key={p.lightId} className="flex gap-2">
                  <button
                    type="button"
                    role="switch"
                    aria-checked={l.output.power}
                    onClick={() => toggle(l)}
                    className="flex min-h-11 flex-1 items-center justify-between rounded-lg border px-3 text-left text-sm hover:border-primary"
                  >
                    <span>{l.name}</span>
                    <span className="text-muted-foreground">{l.output.power ? (l.type === "switch" ? "On" : `${l.output.brightness} %`) : "Off"}</span>
                  </button>
                  <Button variant="outline" className="min-h-11" aria-label={`Controls for ${l.name}`} onClick={() => setSelected(p.lightId)}>Controls</Button>
                </li>
              );
            })}
          </ul>
        </div>
      </MotionConfig>
    </LazyMotion>
  );
}

function toSvgPoint(el: SVGSVGElement, clientX: number, clientY: number) {
  const ctm = el.getScreenCTM();
  if (!ctm) return { x: 0, y: 0 };
  const p = new DOMPoint(clientX, clientY).matrixTransform(ctm.inverse());
  return { x: p.x, y: p.y };
}
