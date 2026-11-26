// Glow mapping (DESIGN §9.3): actual output state -> glow appearance.
// Pure; the input is always the light's reported output (live cache), never
// a locally simulated value.
import type { Light, LightState } from "@/schemas";

export interface Glow {
  on: boolean;
  opacity: number; // 0 when off, 0.15..1 when on
  radiusScale: number; // 0.6..1 of the configured radius
  color: string; // css colour
}

/** Approximate CCT -> RGB (Tanner Helland), good enough for a glow tint. */
export function kelvinToRgb(k: number): [number, number, number] {
  const t = Math.min(40000, Math.max(1000, k)) / 100;
  const r = t <= 66 ? 255 : 329.698727446 * (t - 60) ** -0.1332047592;
  const g = t <= 66 ? 99.4708025861 * Math.log(t) - 161.1195681661 : 288.1221695283 * (t - 60) ** -0.0755148492;
  const b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * Math.log(t - 10) - 305.0447927307;
  const c = (v: number) => Math.round(Math.min(255, Math.max(0, v)));
  return [c(r), c(g), c(b)];
}

const hex = (r: number, g: number, b: number) => `#${[r, g, b].map((v) => v.toString(16).padStart(2, "0")).join("")}`;

export function glowFor(light: Pick<Light, "type"> & { output: LightState }, override?: string): Glow {
  const s = light.output;
  const on = s.power && (light.type === "switch" || s.brightness > 0);
  const level = light.type === "switch" ? 1 : s.brightness / 100;
  let color: string;
  if (override) color = override;
  else if (light.type === "rgb" || light.type === "rgbw" || light.type.startsWith("addressable")) color = hex(s.red, s.green, s.blue);
  else if (light.type === "cct" && s.colorTemperature > 0) color = hex(...kelvinToRgb(s.colorTemperature));
  else color = "#ffd27f"; // warm white for dimmers / switches
  return {
    on,
    opacity: on ? 0.15 + 0.85 * level : 0,
    radiusScale: on ? 0.6 + 0.4 * level : 0.6,
    color,
  };
}
