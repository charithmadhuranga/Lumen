import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { MemoryRouter } from "react-router-dom";

import { ControllersTab } from "./ControllersTab";

const off = { power: false, brightness: 0, red: 0, green: 0, blue: 0, white: 0, colorTemperature: 0 };
const DATA = {
  self: { id: "lumen-0798", enabled: true, running: true, networkId: "home", maxPeers: 8, hasKey: true },
  peers: [
    { id: "lumen-e704", ip: "10.0.0.9", version: "0.1.0", online: true, lastSeenSec: 1, rssi: -40, lights: [{ id: "desk_led", name: "Desk LED", type: "switch", output: off }] },
    { id: "lumen-aaaa", ip: "", version: "0.1.0", online: false, lastSeenSec: 90, rssi: -80, lights: [{ id: "x", name: "Porch", type: "dimmer", output: off }] },
  ],
  stats: { tx: 1 },
};
const ok = (data: unknown, status = 200) => Promise.resolve(new Response(JSON.stringify({ success: true, data, error: null }), { status }));

function renderTab(data: unknown) {
  const f = vi.fn((url: string) => (String(url).includes("/command") ? ok({ sent: true }, 202) : ok(data)));
  vi.stubGlobal("fetch", f);
  render(
    <MemoryRouter>
      <QueryClientProvider client={new QueryClient({ defaultOptions: { queries: { retry: false } } })}>
        <ControllersTab />
      </QueryClientProvider>
    </MemoryRouter>,
  );
  return f;
}

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe("ControllersTab", () => {
  it("lists peers with status and sends commands to the owner", async () => {
    const f = renderTab(DATA);
    expect(await screen.findByRole("region", { name: "Controller lumen-e704" })).toBeTruthy();
    expect(screen.getByRole("status", { name: /offline · last seen 90 s ago/i })).toBeTruthy();
    const desk = screen.getByRole("switch", { name: /Desk LED/ });
    expect(desk.getAttribute("aria-checked")).toBe("false");
    fireEvent.click(desk);
    await waitFor(() => expect(f.mock.calls.some(([u]) => String(u).endsWith("/controllers/lumen-e704/lights/desk_led/command"))).toBe(true));
    const call = f.mock.calls.find(([u]) => String(u).includes("/command"))! as unknown as [string, RequestInit];
    expect(JSON.parse(String(call[1].body))).toEqual({ power: true });
    expect((screen.getByRole("switch", { name: /Porch/ }) as HTMLButtonElement).disabled).toBe(true); // offline peer
  });
  it("explains how to enable when off", async () => {
    renderTab({ ...DATA, self: { ...DATA.self, enabled: false }, peers: [] });
    expect(await screen.findByText("Multi-controller is off")).toBeTruthy();
  });
});
