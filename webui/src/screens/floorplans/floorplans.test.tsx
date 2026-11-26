import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";

import type { Light } from "@/schemas";
import FloorPlanViewer from "./FloorPlanViewer";
import { fit, gridFor, screenToSvg, snap, svgToScreen, zoomAt, type ViewBox } from "./coords";
import { align, distribute, isDirty, useEditorStore } from "./editorStore";
import { glowFor, kelvinToRgb } from "./glow";
import type { FloorPlanT } from "./api";

describe("coords (shared by viewer and editor)", () => {
  const vb: ViewBox = [-50, 20, 800, 500];
  const size = { width: 1000, height: 400 };
  it("round-trips svg <-> screen at any zoom/pan", () => {
    for (const view of [{ scale: 1, panX: 0, panY: 0 }, { scale: 3.5, panX: -120, panY: 44 }]) {
      for (const p of [{ x: -50, y: 20 }, { x: 300.25, y: 250.5 }, { x: 750, y: 520 }]) {
        const back = screenToSvg(svgToScreen(p, vb, size, view), vb, size, view);
        expect(back.x).toBeCloseTo(p.x, 9);
        expect(back.y).toBeCloseTo(p.y, 9);
      }
    }
  });
  it("editor stage transform places a pin exactly where the viewer draws it", () => {
    // Konva stage: scale = fit.scale * zoom, position = fit.offset * zoom + pan (FloorPlanEditor).
    const view = { scale: 2, panX: 30, panY: -10 };
    const f = fit(vb, size.width, size.height);
    const pin = { x: 412.5, y: 133 };
    const konva = { x: pin.x * f.scale * view.scale + f.offsetX * view.scale + view.panX, y: pin.y * f.scale * view.scale + f.offsetY * view.scale + view.panY };
    const viewer = svgToScreen(pin, vb, size, view);
    expect(konva.x).toBeCloseTo(viewer.x, 9);
    expect(konva.y).toBeCloseTo(viewer.y, 9);
  });
  it("fits like preserveAspectRatio xMidYMid meet", () => {
    const f = fit([0, 0, 800, 400], 400, 400);
    expect(f.scale).toBe(0.5);
    expect(f.offsetY).toBe(100); // centred vertically
  });
  it("snaps, picks grids and clamps zoom", () => {
    expect(snap(13, 5)).toBe(15);
    expect(gridFor([0, 0, 800, 500])).toBe(20);
    expect(zoomAt({ scale: 5, panX: 0, panY: 0 }, 10, { x: 0, y: 0 }).scale).toBe(6);
    const z = zoomAt({ scale: 1, panX: 0, panY: 0 }, 2, { x: 100, y: 50 });
    expect(z.panX + 100 * z.scale).toBeCloseTo(100, 9); // the content point under the cursor stays put
  });
});

const L = (over: Partial<Light>): Light => ({
  id: "l1", name: "Lamp", type: "dimmer", roomId: "", driver: { type: "pca9685", deviceId: "p", channel: 0 }, transitionMs: 0, startup: "restore",
  limits: { minBrightness: 0, maxBrightness: 100, colorTemperatureMin: 2700, colorTemperatureMax: 6500 }, calibration: { red: 1, green: 1, blue: 1, white: 1 },
  state: { power: false, brightness: 0, red: 0, green: 0, blue: 0, white: 0, colorTemperature: 0 },
  output: { power: false, brightness: 0, red: 0, green: 0, blue: 0, white: 0, colorTemperature: 0 }, available: true, ...over,
});

describe("glow mapping", () => {
  it("off is invisible; brightness maps to opacity 0.15–1 and radius 60–100 %", () => {
    expect(glowFor(L({})).opacity).toBe(0);
    const half = glowFor(L({ output: { ...L({}).output, power: true, brightness: 50 } }));
    expect(half.opacity).toBeCloseTo(0.575);
    expect(half.radiusScale).toBeCloseTo(0.8);
    const full = glowFor(L({ output: { ...L({}).output, power: true, brightness: 100 } }));
    expect(full.opacity).toBe(1);
  });
  it("uses the light's colour, CCT tint or the user's override", () => {
    expect(glowFor(L({ type: "rgb", output: { ...L({}).output, power: true, brightness: 100, red: 255, green: 0, blue: 16 } })).color).toBe("#ff0010");
    expect(kelvinToRgb(2700)[2]).toBeLessThan(kelvinToRgb(6500)[2]);
    expect(glowFor(L({}), "#123456").color).toBe("#123456");
  });
});

describe("editor store", () => {
  it("undo/redo, coalesced drags and dirty tracking", () => {
    const s = useEditorStore.getState();
    s.load("p", { lights: [{ lightId: "a", x: 0, y: 0, radius: 5 }], rooms: [] });
    expect(isDirty(useEditorStore.getState())).toBe(false);
    s.change((d) => ({ ...d, lights: d.lights.map((l) => ({ ...l, x: 10 })) }));
    s.change((d) => ({ ...d, lights: d.lights.map((l) => ({ ...l, x: 20 })) }), true); // same drag
    expect(useEditorStore.getState().past.length).toBe(1);
    expect(isDirty(useEditorStore.getState())).toBe(true);
    useEditorStore.getState().undo();
    expect(useEditorStore.getState().draft.lights[0].x).toBe(0);
    useEditorStore.getState().redo();
    expect(useEditorStore.getState().draft.lights[0].x).toBe(20);
  });
  it("aligns and distributes", () => {
    const d = { lights: [{ lightId: "a", x: 0, y: 5, radius: 1 }, { lightId: "b", x: 7, y: 9, radius: 1 }, { lightId: "c", x: 30, y: 1, radius: 1 }], rooms: [] };
    expect(align(d, ["a", "b", "c"], "top").lights.map((l) => l.y)).toEqual([1, 1, 1]);
    expect(distribute(d, ["a", "b", "c"], "x").lights.map((l) => l.x)).toEqual([0, 15, 30]);
  });
});

describe("FloorPlanViewer", () => {
  afterEach(() => {
    cleanup();
    vi.unstubAllGlobals();
  });
  const plan: FloorPlanT = { schema_version: 1, id: "g", name: "Ground", viewBox: [0, 0, 200, 100], hasSvg: true, lights: [{ lightId: "l1", x: 50, y: 50, radius: 20 }, { lightId: "gone", x: 1, y: 1, radius: 5 }], rooms: [] };
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100"><rect width="200" height="100" onclick="alert(1)"/><script>alert(1)</script></svg>`;

  function renderViewer(light: Light) {
    const fetchMock = vi.fn(() => Promise.resolve(new Response(JSON.stringify({ success: true, data: { accepted: true }, error: null }), { status: 202 })));
    vi.stubGlobal("fetch", fetchMock);
    const qc = new QueryClient();
    const view = render(
      <QueryClientProvider client={qc}>
        <FloorPlanViewer plan={plan} svg={svg} lights={[light]} rooms={[]} />
      </QueryClientProvider>,
    );
    return { fetchMock, view };
  }

  it("shows actual output (not the requested state) and never renders unsafe markup", () => {
    // Requested ON but hardware still OFF -> no glow, switch not checked.
    const { view } = renderViewer(L({ state: { ...L({}).state, power: true, brightness: 80 } }));
    expect(screen.getByTestId("glow-l1").getAttribute("data-on")).toBe("false");
    expect(screen.getAllByRole("switch", { name: "Lamp" })[0].getAttribute("aria-checked")).toBe("false");
    expect(view.container.innerHTML).not.toMatch(/<script|onclick/);
    expect(screen.queryByTestId("glow-gone")).toBeNull(); // deleted lights are not drawn
  });

  it("follows the output when the controller reports it and toggles through the API", async () => {
    const { fetchMock } = renderViewer(L({ output: { ...L({}).output, power: true, brightness: 100 } }));
    expect(screen.getByTestId("glow-l1").getAttribute("data-on")).toBe("true");
    fireEvent.click(screen.getAllByRole("switch", { name: "Lamp" })[0]);
    await waitFor(() => expect(fetchMock).toHaveBeenCalled());
    const [url, init] = fetchMock.mock.calls[0] as unknown as [string, RequestInit];
    expect(url).toBe("/api/v1/lights/l1/command");
    expect(JSON.parse(String(init.body))).toEqual({ toggle: true });
  });
});
