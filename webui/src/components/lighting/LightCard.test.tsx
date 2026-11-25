import { afterEach, describe, expect, it } from "vitest";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";

import { LightCard } from "@/components/lighting/LightCard";
import type { Light } from "@/schemas";

const base: Light = {
  id: "l1",
  name: "Desk",
  type: "dimmer",
  roomId: "",
  driver: { type: "pca9685", deviceId: "pca9685_0", channel: 0 },
  transitionMs: 500,
  startup: "restore",
  limits: { minBrightness: 0, maxBrightness: 100, colorTemperatureMin: 2700, colorTemperatureMax: 6500 },
  calibration: { red: 1, green: 1, blue: 1, white: 1 },
  state: { power: true, brightness: 60, red: 0, green: 0, blue: 0, white: 0, colorTemperature: 0 },
  output: { power: true, brightness: 60, red: 0, green: 0, blue: 0, white: 0, colorTemperature: 0 },
  available: true,
};

function renderCard(light: Light) {
  const qc = new QueryClient();
  qc.setQueryData(["lights", "effects"], ["breathing"]);
  return render(
    <QueryClientProvider client={qc}>
      <LightCard light={light} />
    </QueryClientProvider>,
  );
}

afterEach(cleanup);

describe("LightCard type-aware controls (DESIGN §5.3)", () => {
  it("switch: power only", () => {
    renderCard({ ...base, type: "switch" });
    expect(screen.getByRole("button", { name: /desk: on/i })).toBeTruthy();
    expect(screen.queryByRole("slider")).toBeNull();
    expect(screen.queryByRole("button", { name: /show controls/i })).toBeNull();
  });

  it("dimmer: brightness, no colour", () => {
    renderCard(base);
    expect(screen.getByRole("slider", { name: /desk brightness/i })).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: /show controls/i }));
    expect(screen.queryByRole("slider", { name: /hue/i })).toBeNull();
    expect(screen.queryByRole("slider", { name: /colour temperature/i })).toBeNull();
  });

  it("rgb: colour picker but no white or CCT", () => {
    renderCard({ ...base, type: "rgb", state: { ...base.state, red: 255 } });
    fireEvent.click(screen.getByRole("button", { name: /show controls/i }));
    expect(screen.getByRole("slider", { name: /hue/i })).toBeTruthy();
    expect(screen.queryByRole("slider", { name: /desk white/i })).toBeNull();
    expect(screen.queryByRole("slider", { name: /colour temperature/i })).toBeNull();
  });

  it("cct: warm/cool slider", () => {
    renderCard({ ...base, type: "cct", state: { ...base.state, colorTemperature: 3000 } });
    fireEvent.click(screen.getByRole("button", { name: /show controls/i }));
    expect(screen.getByRole("slider", { name: /colour temperature/i })).toBeTruthy();
    expect(screen.queryByRole("slider", { name: /hue/i })).toBeNull();
  });

  it("flags lights without working hardware", () => {
    renderCard({ ...base, available: false });
    expect(screen.getByText(/hardware unavailable/i)).toBeTruthy();
  });
});
