import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";

import Integrations from "./Integrations";
import { MqttForm } from "./integrations/api";

const DATA = {
  mqtt: {
    enabled: true,
    host: "10.0.0.5",
    port: 1883,
    tls: false,
    username: "lumen",
    hasPassword: true,
    clientId: "",
    topicPrefix: "lighting",
    controllerId: "",
    qos: 0,
    keepalive: 60,
    retain: true,
    homeAssistant: { enabled: true, prefix: "homeassistant" },
    status: { connected: true, broker: "mqtt://10.0.0.5:1883", lastError: "", published: 12, received: 3, rejected: 0, reconnects: 0, dropped: 0 },
  },
  control: {
    tcpEnabled: false,
    tcpPort: 8266,
    udpEnabled: true,
    udpControl: false,
    udpAnnounce: false,
    udpPort: 8267,
    status: { tcpClients: 0, tcpCommands: 0, tcpAuthFailures: 0, udpCommands: 0, udpRejected: 0, discoveryReplies: 0 },
  },
  baseTopic: "lighting/lumen-0798",
};

const ok = (data: unknown) =>
  Promise.resolve(new Response(JSON.stringify({ success: true, data, error: null }), { status: 200, headers: { "Content-Type": "application/json" } }));

function renderScreen(fetchImpl: (url: string, init?: RequestInit) => Promise<Response>) {
  const f = vi.fn(fetchImpl);
  vi.stubGlobal("fetch", f);
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <Integrations />
    </QueryClientProvider>,
  );
  return f;
}

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe("MQTT form schema", () => {
  const base = { enabled: true, host: "h", port: 1883, tls: false, username: "", password: "", clientId: "", topicPrefix: "lighting", controllerId: "", qos: 0 as const, keepalive: 60, retain: true };
  it("requires a host only when enabled", () => {
    expect(MqttForm.safeParse({ ...base, host: "" }).success).toBe(false);
    expect(MqttForm.safeParse({ ...base, enabled: false, host: "" }).success).toBe(true);
  });
  it("rejects MQTT wildcards and separators in topic parts", () => {
    for (const bad of ["a/b", "a+", "#", "with space"]) expect(MqttForm.safeParse({ ...base, topicPrefix: bad }).success).toBe(false);
    expect(MqttForm.safeParse({ ...base, controllerId: "" }).success).toBe(true);
  });
  it("bounds port and keepalive", () => {
    expect(MqttForm.safeParse({ ...base, port: 0 }).success).toBe(false);
    expect(MqttForm.safeParse({ ...base, port: 70000 }).success).toBe(false);
    expect(MqttForm.safeParse({ ...base, keepalive: 5 }).success).toBe(false);
  });
});

describe("Integrations screen", () => {
  it("shows the connection status and never pre-fills the password", async () => {
    renderScreen(() => ok(DATA));
    expect(await screen.findByRole("status", { name: /connected · mqtt:\/\/10\.0\.0\.5:1883/i })).toBeTruthy();
    expect((screen.getByLabelText("Password") as HTMLInputElement).value).toBe("");
    expect(screen.getByText(/leave empty to keep/i)).toBeTruthy();
    expect(screen.queryByLabelText("Integration token")).toBeNull(); // only on request
  });

  it("keeps the stored password when the field is left empty", async () => {
    const f = renderScreen(() => ok(DATA));
    fireEvent.change(await screen.findByLabelText("Broker address"), { target: { value: "broker.lan" } });
    fireEvent.click(screen.getByRole("button", { name: "Save MQTT" }));
    await waitFor(() => expect(f.mock.calls.some(([, i]) => i?.method === "PUT")).toBe(true));
    const put = f.mock.calls.find(([, i]) => i?.method === "PUT")!;
    expect(String(put[0])).toContain("/integrations/mqtt");
    const body = JSON.parse(String(put[1]!.body)) as Record<string, unknown>;
    expect(body.host).toBe("broker.lan");
    expect("password" in body).toBe(false);
  });

  it("blocks saving an enabled broker without an address", async () => {
    const f = renderScreen(() => ok(DATA));
    fireEvent.change(await screen.findByLabelText("Broker address"), { target: { value: "" } });
    fireEvent.click(screen.getByRole("button", { name: "Save MQTT" }));
    expect(await screen.findByText("Broker address is required")).toBeTruthy();
    expect(f.mock.calls.some(([, i]) => i?.method === "PUT")).toBe(false);
  });

  it("reveals the integration token only when asked", async () => {
    renderScreen((url) => (String(url).endsWith("/integrations/token") ? ok({ token: "abc123" }) : ok(DATA)));
    fireEvent.click(await screen.findByRole("button", { name: /show token/i }));
    expect((await screen.findByLabelText("Integration token")).textContent).toBe("abc123");
  });
});

describe("Lumen Cloud card", () => {
  const OFF = { mode: "off", state: "off", lastError: null, gatewaySeenSecAgo: null, tickets: { accepted: 0, rejected: 0, rejectedBy: {} } };
  const PAIRED = {
    ...OFF,
    mode: "gateway",
    state: "paired",
    deviceId: "0193f6c2-7a10-7b44-9d92-001122334455",
    gatewayId: "0193f6c2-7a10-7c00-8a00-00000000aa01",
    gatewayUrl: "http://192.168.1.20:8780",
    ticketEpoch: 0,
    pairedAt: 0,
    gatewaySeenSecAgo: 12,
    tickets: { accepted: 5, rejected: 1, rejectedBy: { expired: 1 } },
  };
  const route = (cloud: unknown) => (url: string) => (String(url).endsWith("/cloud") ? ok(cloud) : ok(DATA));

  it("validates the claim code and gateway address before pairing", async () => {
    const f = renderScreen(route(OFF));
    fireEvent.change(await screen.findByLabelText("Claim code"), { target: { value: "not-a-code" } });
    fireEvent.change(screen.getByLabelText("Gateway address"), { target: { value: "http://8.8.8.8" } });
    fireEvent.click(screen.getByRole("button", { name: /pair with gateway/i }));
    expect(await screen.findByText(/starts with lct_/i)).toBeTruthy();
    expect(screen.getByText(/gateway's local address/i)).toBeTruthy();
    expect(f.mock.calls.some(([u, i]) => String(u).endsWith("/cloud/pair") && i?.method === "POST")).toBe(false);
  });

  it("sends the pairing request", async () => {
    const f = renderScreen((url, init) =>
      String(url).endsWith("/cloud/pair") && init?.method === "POST" ? ok({ ...OFF, state: "pairing" }) : route(OFF)(url),
    );
    const claim = "lct_" + "A".repeat(43);
    fireEvent.change(await screen.findByLabelText("Claim code"), { target: { value: claim } });
    fireEvent.change(screen.getByLabelText("Gateway address"), { target: { value: "http://192.168.1.20:8780" } });
    fireEvent.click(screen.getByRole("button", { name: /pair with gateway/i }));
    await waitFor(() => expect(f.mock.calls.some(([u]) => String(u).endsWith("/cloud/pair"))).toBe(true));
    const call = f.mock.calls.find(([u]) => String(u).endsWith("/cloud/pair"))!;
    expect(JSON.parse(String(call[1]!.body))).toEqual({ claimToken: claim, gatewayUrl: "http://192.168.1.20:8780" });
    expect(await screen.findByRole("button", { name: /pairing…/i })).toBeTruthy();
  });

  it("shows the pairing and offers unpair, never a key", async () => {
    renderScreen(route(PAIRED));
    expect(await screen.findByText("http://192.168.1.20:8780")).toBeTruthy();
    expect(screen.getByText("Just now")).toBeTruthy();
    expect(screen.getByRole("button", { name: /unpair/i })).toBeTruthy();
    expect(screen.queryByLabelText("Claim code")).toBeNull();
  });

  it("explains a failed pairing", async () => {
    renderScreen(route({ ...OFF, state: "pair_failed", lastError: "the gateway refused pairing: The claim code is invalid" }));
    expect((await screen.findByRole("alert")).textContent).toMatch(/claim code is invalid/);
  });
});
