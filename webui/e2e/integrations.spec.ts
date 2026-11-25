// Real-device flow: enable the TCP control server from the Integrations
// screen, take the integration token from the UI, drive a PCA9685 output over
// a raw TCP socket and verify it through register read-back.
import net from "node:net";
import { expect, test } from "@playwright/test";

import { apiSession, signIn } from "./helpers";

test.describe.configure({ mode: "serial" });

function tcpSession(host: string, port: number) {
  const sock = net.connect(port, host);
  let buf = "";
  const waiters: ((line: string) => void)[] = [];
  sock.setEncoding("utf8");
  sock.on("data", (d: string) => {
    buf += d;
    for (let i = buf.indexOf("\n"); i >= 0; i = buf.indexOf("\n")) {
      const line = buf.slice(0, i);
      buf = buf.slice(i + 1);
      waiters.shift()?.(line);
    }
  });
  const ready = new Promise<void>((res, rej) => {
    sock.once("connect", () => res());
    sock.once("error", rej);
  });
  // Send one request and resolve with the next reply that is not a state push.
  const call = async (msg: object) => {
    await ready;
    for (sock.write(JSON.stringify(msg) + "\n"); ; ) {
      const line = await new Promise<string>((res, rej) => {
        const t = setTimeout(() => rej(new Error("TCP reply timeout")), 5000);
        waiters.push((l) => (clearTimeout(t), res(l)));
      });
      const o = JSON.parse(line) as Record<string, unknown>;
      if (o.type !== "state") return o;
    }
  };
  return { call, close: () => sock.destroy() };
}

test("TCP control enabled from the UI drives real hardware", async ({ page, request, baseURL }) => {
  const api = await apiSession(request);
  await api("DELETE", "/lights/e2e_tcp");
  const light = await api("POST", "/lights", {
    id: "e2e_tcp",
    name: "E2E TCP",
    type: "dimmer",
    driver: { type: "pca9685", deviceId: "pca9685_0", channel: 14 },
    transitionMs: 0,
  });
  expect(light.success, JSON.stringify(light.error)).toBe(true);
  const before = (await api("GET", "/integrations")).data as { control: { tcpEnabled: boolean } };

  await signIn(page);
  await page.goto("/integrations");
  const tcp = page.getByRole("switch", { name: /TCP control server/ });
  if ((await tcp.getAttribute("aria-checked")) !== "true") await tcp.click();
  await expect(tcp).toHaveAttribute("aria-checked", "true");
  await page.getByRole("button", { name: /show token/i }).click();
  const token = (await page.getByLabel("Integration token").textContent())!.trim();
  expect(token.length).toBeGreaterThanOrEqual(32);

  const host = new URL(baseURL!).hostname;
  const s = tcpSession(host, 8266);
  try {
    expect(await s.call({ v: 1, type: "auth", token })).toMatchObject({ ok: true });
    expect(await s.call({ v: 1, id: 1, type: "light", light_id: "e2e_tcp", state: "ON", brightness: 100, transition: 0 })).toMatchObject({ id: 1, ok: true });
    await expect
      .poll(async () => ((await api("GET", "/hardware/outputs/pca9685_0:14/readback")).data as { percent: number }).percent)
      .toBe(100);
    expect(await s.call({ v: 1, id: 2, type: "light", light_id: "e2e_tcp", brightness: 101 })).toMatchObject({ id: 2, ok: false });
    expect(await s.call({ v: 1, id: 3, type: "light", light_id: "e2e_tcp", state: "OFF", transition: 0 })).toMatchObject({ ok: true });
    await expect
      .poll(async () => ((await api("GET", "/hardware/outputs/pca9685_0:14/readback")).data as { percent: number }).percent)
      .toBe(0);
  } finally {
    s.close();
    await api("DELETE", "/lights/e2e_tcp");
    if (!before.control.tcpEnabled) await api("PUT", "/integrations/tcp", { tcpEnabled: false });
  }
});
