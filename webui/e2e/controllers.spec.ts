// Two real controllers (Phase 11): set LUMEN_PEER_URL to the second one (same
// password). From controller A's UI, switch a light owned by controller B and
// verify B applied it. Skipped when no peer is configured.
import AxeBuilder from "@axe-core/playwright";
import { expect, request as pwRequest, test } from "@playwright/test";

import { PASSWORD, signIn } from "./helpers";

const PEER = process.env.LUMEN_PEER_URL;
const PEER_LIGHT = process.env.LUMEN_PEER_LIGHT ?? "desk_led";

test("Controllers tab switches a light on another controller", async ({ page }) => {
  test.skip(!PEER, "LUMEN_PEER_URL not set");
  const peer = await pwRequest.newContext({ baseURL: PEER });
  const login = await (await peer.post("/api/v1/auth/login", { data: { username: "admin", password: PASSWORD } })).json();
  const csrf = login.data.csrfToken as string;
  const peerLight = async () => ((await (await peer.get(`/api/v1/lights/${PEER_LIGHT}`)).json()).data.output.power as boolean);
  const peerId = ((await (await peer.get("/api/v1/controllers")).json()).data.self.id) as string;
  await peer.post(`/api/v1/lights/${PEER_LIGHT}/command`, { data: { power: false }, headers: { "X-CSRF-Token": csrf } });

  await signIn(page);
  await page.goto("/devices");
  await page.getByRole("tab", { name: "Controllers" }).click();
  const card = page.getByRole("region", { name: `Controller ${peerId}` });
  await expect(card).toBeVisible({ timeout: 15000 });
  const sw = card.getByRole("switch").filter({ hasText: /./ }).first();
  await expect(sw).toHaveAttribute("aria-checked", "false", { timeout: 8000 });
  await sw.click();
  await expect.poll(peerLight, { timeout: 5000 }).toBe(true);
  await expect(sw).toHaveAttribute("aria-checked", "true", { timeout: 8000 }); // state reported back by the owner
  const axe = await new AxeBuilder({ page }).analyze();
  expect(axe.violations.filter((v) => v.impact === "serious" || v.impact === "critical").map((v) => `${v.id} ${v.nodes[0]?.target}`)).toEqual([]);
  await sw.click();
  await expect.poll(peerLight, { timeout: 5000 }).toBe(false);
  await peer.dispose();
});
