// Edit mode (DESIGN §9.4): React Konva, lazy-loaded (never imported by the
// viewer). The sanitised SVG is shown as a non-listening Konva.Image from a
// Blob URL (image context: scripts can never run). Light pins and room
// regions are Konva nodes whose coordinates ARE viewBox coordinates — the
// stage transform comes from coords.fit(), the same maths as the viewer.
import * as React from "react";
// Core build + only the shapes used here (the full Konva bundle has every
// shape and filter; this keeps the editor chunk inside its budget).
import { Circle, Group, Image as KImage, Layer, Line, Rect, Stage, Text } from "react-konva/lib/ReactKonvaCore";
import "konva/lib/shapes/Circle";
import "konva/lib/shapes/Image";
import "konva/lib/shapes/Line";
import "konva/lib/shapes/Rect";
import "konva/lib/shapes/Text";
import type Konva from "konva";
import { AlignCenterHorizontal, AlignCenterVertical, AlignEndHorizontal, AlignStartHorizontal, Grid3x3, MapPin, Redo2, Square, Trash2, Undo2 } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import type { Light, Room } from "@/schemas";
import { clampToViewBox, fit, gridFor, snap, zoomAt, type View, type ViewBox } from "./coords";
import { align, distribute, useEditorStore, type Draft } from "./editorStore";
import type { FloorPlanT } from "./api";

function useSize(ref: React.RefObject<HTMLDivElement | null>) {
  const [w, setW] = React.useState(0);
  React.useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const ro = new ResizeObserver(([e]) => setW(Math.floor(e.contentRect.width)));
    ro.observe(el);
    setW(Math.floor(el.getBoundingClientRect().width));
    return () => ro.disconnect();
  }, [ref]);
  return w;
}


/** Design tokens for the canvas (Konva cannot read CSS variables); re-read on theme change. */
function useTokens() {
  const read = () => {
    const cs = getComputedStyle(document.documentElement);
    const v = (n: string) => cs.getPropertyValue(n).trim() || "gray";
    return { primary: v("--primary"), warning: v("--warning"), muted: v("--muted-foreground"), background: v("--background"), foreground: v("--foreground") };
  };
  const [t, setT] = React.useState(read);
  React.useEffect(() => {
    const mo = new MutationObserver(() => setT(read()));
    mo.observe(document.documentElement, { attributes: true, attributeFilter: ["class", "data-theme", "style"] });
    return () => mo.disconnect();
  }, []);
  return t;
}

function useSvgImage(svg: string | null, vb: ViewBox) {
  const [img, setImg] = React.useState<HTMLImageElement | null>(null);
  React.useEffect(() => {
    if (!svg) return;
    // Explicit size so the raster matches the viewBox exactly.
    const sized = svg.replace(/<svg\b/, `<svg width="${vb[2]}" height="${vb[3]}"`);
    const url = URL.createObjectURL(new Blob([sized], { type: "image/svg+xml" }));
    const i = new window.Image();
    i.onload = () => setImg(i);
    i.src = url;
    return () => URL.revokeObjectURL(url);
  }, [svg, vb]);
  return img;
}

export default function FloorPlanEditor({ plan, svg, lights, rooms }: { plan: FloorPlanT; svg: string | null; lights: Light[]; rooms: Room[] }) {
  const vb = plan.viewBox as ViewBox;
  const wrap = React.useRef<HTMLDivElement>(null);
  const width = useSize(wrap);
  const height = Math.round(Math.min(width * (vb[3] / vb[2]), typeof window !== "undefined" ? window.innerHeight * 0.65 : 600));
  const image = useSvgImage(svg, vb);
  const t = useTokens();
  const { draft, change, undo, redo, selected, select, past, future } = useEditorStore();
  const [snapOn, setSnapOn] = React.useState(true);
  const [view, setView] = React.useState<View>({ scale: 1, panX: 0, panY: 0 });
  const [roomToAdd, setRoomToAdd] = React.useState<string>("");
  const grid = gridFor(vb);
  const unit = Math.max(vb[2], vb[3]) / 100;
  const f = fit(vb, width || 1, height || 1);
  const byId = React.useMemo(() => new Map(lights.map((l) => [l.id, l])), [lights]);
  const unplaced = lights.filter((l) => !draft.lights.some((p) => p.lightId === l.id));
  const place = (x: number, y: number) => {
    const p = clampToViewBox({ x: snapOn ? snap(x, grid) : x, y: snapOn ? snap(y, grid) : y }, vb);
    return { x: Math.round(p.x * 100) / 100, y: Math.round(p.y * 100) / 100 };
  };
  const selectedPins = draft.lights.filter((l) => selected.includes(l.lightId));
  const selectedRoom = selected.find((s) => s.startsWith("room:"))?.slice(5);

  const addLight = (id: string) => {
    const at = place(vb[0] + vb[2] / 2, vb[1] + vb[3] / 2);
    change((d) => ({ ...d, lights: [...d.lights, { lightId: id, ...at, radius: Math.round(unit * 8) }] }));
    select([id]);
  };
  const addRoom = (roomId: string) => {
    const cx = vb[0] + vb[2] / 2, cy = vb[1] + vb[3] / 2, hw = vb[2] / 6, hh = vb[3] / 6;
    change((d) => ({ ...d, rooms: [...d.rooms.filter((r) => r.roomId !== roomId), { roomId, points: [[cx - hw, cy - hh], [cx + hw, cy - hh], [cx + hw, cy + hh], [cx - hw, cy + hh]] }] }));
    select([`room:${roomId}`]);
    setRoomToAdd("");
  };
  const removeSelected = () => {
    change((d) => ({ lights: d.lights.filter((l) => !selected.includes(l.lightId)), rooms: d.rooms.filter((r) => !selected.includes(`room:${r.roomId}`)) }));
    select([]);
  };
  const nudge = (dx: number, dy: number) =>
    change((d) => ({ ...d, lights: d.lights.map((l) => (selected.includes(l.lightId) ? { ...l, ...clampToViewBox({ x: l.x + dx, y: l.y + dy }, vb) } : l)) }), true);
  const onKey = (e: React.KeyboardEvent) => {
    const step = (snapOn ? grid : unit) * (e.shiftKey ? 5 : 1);
    if (e.key === "ArrowLeft") nudge(-step, 0);
    else if (e.key === "ArrowRight") nudge(step, 0);
    else if (e.key === "ArrowUp") nudge(0, -step);
    else if (e.key === "ArrowDown") nudge(0, step);
    else if (e.key === "Delete" || e.key === "Backspace") removeSelected();
    else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") (e.shiftKey ? redo : undo)();
    else return;
    e.preventDefault();
  };
  const toggleSelect = (id: string, additive: boolean) => select(additive ? (selected.includes(id) ? selected.filter((s) => s !== id) : [...selected, id]) : [id]);
  const onWheel = (e: Konva.KonvaEventObject<WheelEvent>) => {
    e.evt.preventDefault();
    const p = e.target.getStage()?.getPointerPosition();
    if (p) setView((v) => zoomAt(v, e.evt.deltaY < 0 ? 1.15 : 1 / 1.15, p));
  };

  return (
    <div className="grid gap-4 lg:grid-cols-[1fr_18rem]">
      <div className="space-y-2">
        <div className="flex flex-wrap items-center gap-1" role="toolbar" aria-label="Editor tools">
          <Button size="icon" variant="outline" aria-label="Undo" onClick={undo} disabled={past.length === 0}><Undo2 className="size-4" /></Button>
          <Button size="icon" variant="outline" aria-label="Redo" onClick={redo} disabled={future.length === 0}><Redo2 className="size-4" /></Button>
          <Button size="icon" variant="outline" aria-label="Delete selected" onClick={removeSelected} disabled={selected.length === 0}><Trash2 className="size-4" /></Button>
          <span className="mx-1 h-6 w-px bg-border" aria-hidden="true" />
          <Button size="icon" variant="outline" aria-label="Align left" onClick={() => change((d) => align(d, selected, "left"))} disabled={selectedPins.length < 2}><AlignStartHorizontal className="size-4" /></Button>
          <Button size="icon" variant="outline" aria-label="Align centres horizontally" onClick={() => change((d) => align(d, selected, "centerY"))} disabled={selectedPins.length < 2}><AlignCenterHorizontal className="size-4" /></Button>
          <Button size="icon" variant="outline" aria-label="Align centres vertically" onClick={() => change((d) => align(d, selected, "centerX"))} disabled={selectedPins.length < 2}><AlignCenterVertical className="size-4" /></Button>
          <Button size="icon" variant="outline" aria-label="Distribute horizontally" onClick={() => change((d) => distribute(d, selected, "x"))} disabled={selectedPins.length < 3}><AlignEndHorizontal className="size-4" /></Button>
          <span className="mx-1 h-6 w-px bg-border" aria-hidden="true" />
          <label className="flex min-h-11 items-center gap-2 px-1 text-sm">
            <Switch checked={snapOn} onCheckedChange={setSnapOn} aria-label="Snap to grid" />
            <Grid3x3 className="size-4" aria-hidden="true" /> Snap
          </label>
        </div>
        <div
          ref={wrap}
          tabIndex={0}
          role="application"
          onKeyDown={onKey}
          aria-label="Floor plan editing canvas. Arrow keys move the selection, Delete removes it."
          className="overflow-hidden rounded-xl border-2 border-dashed border-primary bg-muted/40 outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          {width > 0 && (
            <Stage
              width={width}
              height={height}
              scaleX={f.scale * view.scale}
              scaleY={f.scale * view.scale}
              x={f.offsetX * view.scale + view.panX}
              y={f.offsetY * view.scale + view.panY}
              onWheel={onWheel}
              onMouseDown={(e) => e.target === e.target.getStage() && select([])}
              onTap={(e) => e.target === e.target.getStage() && select([])}
            >
              <Layer listening={false}>
                <Rect x={vb[0]} y={vb[1]} width={vb[2]} height={vb[3]} fill={t.background} opacity={0.6} />
                {image && <KImage image={image} x={vb[0]} y={vb[1]} width={vb[2]} height={vb[3]} />}
              </Layer>
              <Layer>
                {draft.rooms.map((r) => {
                  const sel = selectedRoom === r.roomId;
                  const name = rooms.find((x) => x.id === r.roomId)?.name ?? r.roomId;
                  return (
                    <Group key={r.roomId}>
                      <Line points={r.points.flat()} closed fill={t.primary} opacity={sel ? 0.2 : 0.08} listening={false} />
                      <Line
                        points={r.points.flat()}
                        closed
                        fillEnabled={false}
                        hitStrokeWidth={unit * 2}
                        stroke={t.primary}
                        strokeWidth={unit * (sel ? 0.5 : 0.25)}
                        draggable
                        onClick={(e) => toggleSelect(`room:${r.roomId}`, e.evt.shiftKey)}
                        onTap={() => toggleSelect(`room:${r.roomId}`, false)}
                        onDragEnd={(e) => {
                          const dx = e.target.x(), dy = e.target.y();
                          e.target.position({ x: 0, y: 0 });
                          change((d) => ({ ...d, rooms: d.rooms.map((x) => (x.roomId === r.roomId ? { ...x, points: x.points.map((p) => { const q = place(p[0] + dx, p[1] + dy); return [q.x, q.y] as [number, number]; }) } : x)) }));
                        }}
                      />
                      <Text x={r.points[0][0] + unit} y={r.points[0][1] + unit} text={name} fontSize={unit * 2.5} fill={t.primary} listening={false} />
                      {sel &&
                        r.points.map((p, i) => (
                          <Circle
                            key={i}
                            x={p[0]}
                            y={p[1]}
                            radius={unit * 1.2}
                            fill={t.background}
                            stroke={t.primary}
                            strokeWidth={unit * 0.3}
                            draggable
                            onDragEnd={(e) => {
                              const q = place(e.target.x(), e.target.y());
                              change((d) => ({ ...d, rooms: d.rooms.map((x) => (x.roomId === r.roomId ? { ...x, points: x.points.map((pt, j) => (j === i ? [q.x, q.y] : pt)) as [number, number][] } : x)) }));
                            }}
                          />
                        ))}
                    </Group>
                  );
                })}
                {draft.lights.map((p) => {
                  const l = byId.get(p.lightId);
                  const sel = selected.includes(p.lightId);
                  return (
                    <Group
                      key={p.lightId}
                      x={p.x}
                      y={p.y}
                      draggable
                      onClick={(e) => toggleSelect(p.lightId, e.evt.shiftKey)}
                      onTap={() => toggleSelect(p.lightId, false)}
                      onDragStart={() => !sel && select([p.lightId])}
                      onDragEnd={(e) => {
                        const q = place(e.target.x(), e.target.y());
                        e.target.position(q);
                        change((d) => ({ ...d, lights: d.lights.map((x) => (x.lightId === p.lightId ? { ...x, ...q } : x)) }));
                      }}
                    >
                      <Circle radius={p.radius} fill={p.color ?? t.warning} opacity={0.18} listening={false} />
                      <Circle radius={p.radius} stroke={sel ? t.primary : t.warning} strokeWidth={unit * (sel ? 0.4 : 0.15)} dash={[unit, unit]} listening={false} />
                      <Circle radius={unit * 1.4} fill={l ? t.warning : t.muted} stroke={t.background} strokeWidth={unit * 0.35} />
                      <Text text={l?.name ?? `${p.lightId} (deleted)`} y={unit * 2} fontSize={unit * 2.2} fill={t.foreground} align="center" width={unit * 30} offsetX={unit * 15} listening={false} />
                    </Group>
                  );
                })}
              </Layer>
            </Stage>
          )}
        </div>
        <p className="text-xs text-muted-foreground">Drag lights and room corners. Shift-click to select several. Scroll to zoom. Arrow keys nudge, Delete removes, Ctrl+Z undoes.</p>
      </div>

      <aside className="space-y-4">
        <section className="space-y-2 rounded-xl border bg-card p-3">
          <h2 className="text-sm font-semibold">Place lights</h2>
          {unplaced.length === 0 ? (
            <p className="text-sm text-muted-foreground">All lights are on the plan.</p>
          ) : (
            <ul className="max-h-56 space-y-1 overflow-auto">
              {unplaced.map((l) => (
                <li key={l.id}>
                  <button type="button" onClick={() => addLight(l.id)} className="flex min-h-11 w-full items-center gap-2 rounded-md px-2 text-left text-sm hover:bg-muted">
                    <MapPin className="size-4" aria-hidden="true" /> {l.name}
                  </button>
                </li>
              ))}
            </ul>
          )}
        </section>
        <section className="space-y-2 rounded-xl border bg-card p-3">
          <h2 className="text-sm font-semibold">Room regions</h2>
          <div className="flex gap-2">
            <Select value={roomToAdd} onValueChange={setRoomToAdd}>
              <SelectTrigger aria-label="Room to outline"><SelectValue placeholder="Choose a room" /></SelectTrigger>
              <SelectContent>{rooms.map((r) => <SelectItem key={r.id} value={r.id}>{r.name}</SelectItem>)}</SelectContent>
            </Select>
            <Button variant="outline" onClick={() => roomToAdd && addRoom(roomToAdd)} disabled={!roomToAdd} aria-label="Add room region"><Square className="size-4" /></Button>
          </div>
        </section>
        {selectedPins.length === 1 && <PinPanel pin={selectedPins[0]} light={byId.get(selectedPins[0].lightId)} max={Math.max(vb[2], vb[3]) / 2} />}
      </aside>
    </div>
  );
}

function PinPanel({ pin, light, max }: { pin: Draft["lights"][number]; light?: Light; max: number }) {
  const change = useEditorStore((s) => s.change);
  const set = (patch: Partial<Draft["lights"][number]>, coalesce = false) =>
    change((d) => ({ ...d, lights: d.lights.map((l) => (l.lightId === pin.lightId ? { ...l, ...patch } : l)) }), coalesce);
  return (
    <section className="space-y-3 rounded-xl border bg-card p-3">
      <h2 className="text-sm font-semibold">{light?.name ?? pin.lightId}</h2>
      <label className="block space-y-1 text-sm">
        <span>Glow radius</span>
        <input type="range" min={Math.max(1, max / 100)} max={max} step={max / 200} value={pin.radius} onChange={(e) => set({ radius: Number(e.target.value) }, true)} className="w-full" aria-label="Glow radius" />
      </label>
      <div className="flex items-center gap-2 text-sm">
        <span>Glow colour</span>
        <Input type="color" value={pin.color ?? "#ffd27f"} onChange={(e) => set({ color: e.target.value }, true)} className="h-11 w-16 p-1" aria-label="Glow colour" />
        {pin.color && <Button variant="ghost" size="sm" onClick={() => set({ color: undefined })}>Use light colour</Button>}
      </div>
      <p className="text-xs text-muted-foreground">x {pin.x.toFixed(1)} · y {pin.y.toFixed(1)} (drawing units)</p>
    </section>
  );
}
