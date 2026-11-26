// Shared coordinate core (DESIGN §9.1): every stored position is in SVG
// viewBox space. The viewer (inline SVG) and the editor (Konva) both convert
// through these functions, so a light placed in one appears at the same spot
// in the other.

export type ViewBox = [number, number, number, number];

/** Uniform "xMidYMid meet" fit of a viewBox into a w×h box (what <svg> does). */
export function fit(vb: ViewBox, width: number, height: number) {
  const scale = Math.min(width / vb[2], height / vb[3]);
  return {
    scale,
    offsetX: (width - vb[2] * scale) / 2 - vb[0] * scale,
    offsetY: (height - vb[3] * scale) / 2 - vb[1] * scale,
  };
}

export interface View {
  scale: number; // user zoom (1 = fitted)
  panX: number; // screen pixels
  panY: number;
}

export const MIN_ZOOM = 0.5;
export const MAX_ZOOM = 6;

export function svgToScreen(p: { x: number; y: number }, vb: ViewBox, size: { width: number; height: number }, view: View = { scale: 1, panX: 0, panY: 0 }) {
  const f = fit(vb, size.width, size.height);
  return { x: (p.x * f.scale + f.offsetX) * view.scale + view.panX, y: (p.y * f.scale + f.offsetY) * view.scale + view.panY };
}

export function screenToSvg(p: { x: number; y: number }, vb: ViewBox, size: { width: number; height: number }, view: View = { scale: 1, panX: 0, panY: 0 }) {
  const f = fit(vb, size.width, size.height);
  return { x: ((p.x - view.panX) / view.scale - f.offsetX) / f.scale, y: ((p.y - view.panY) / view.scale - f.offsetY) / f.scale };
}

export function snap(value: number, grid: number) {
  return grid > 0 ? Math.round(value / grid) * grid : value;
}

/** A sensible grid for a drawing: ~1/50 of its larger side, rounded. */
export function gridFor(vb: ViewBox) {
  const raw = Math.max(vb[2], vb[3]) / 50;
  const p = 10 ** Math.floor(Math.log10(raw));
  return [1, 2, 5, 10].map((m) => m * p).find((g) => g >= raw) ?? raw;
}

export function clampZoom(z: number) {
  return Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, z));
}

/** Zoom around a screen point (keeps that point fixed). */
export function zoomAt(view: View, factor: number, at: { x: number; y: number }): View {
  const scale = clampZoom(view.scale * factor);
  const k = scale / view.scale;
  return { scale, panX: at.x - (at.x - view.panX) * k, panY: at.y - (at.y - view.panY) * k };
}

export function clampToViewBox(p: { x: number; y: number }, vb: ViewBox) {
  return { x: Math.min(vb[0] + vb[2], Math.max(vb[0], p.x)), y: Math.min(vb[1] + vb[3], Math.max(vb[1], p.y)) };
}
