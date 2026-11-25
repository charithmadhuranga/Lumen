// Build an automation in the Simple editor, then prove the controller runs
// it on real hardware (PCA9685 read-back).
import { expect, test } from "@playwright/test";

import { apiSession, signIn } from "./helpers";

test("automation created in the UI runs on the controller", async ({ page, request }) => {
  const api = await apiSession(request);
  for (const id of ["e2e_src", "e2e_dst"]) await api("DELETE", `/lights/${id}`);
  await api("DELETE", "/automations/e2e_follow");
  const mk = (id: string, name: string, ch: number) =>
    api("POST", "/lights", { id, name, type: "dimmer", driver: { type: "pca9685", deviceId: "pca9685_0", channel: ch }, transitionMs: 0 });
  expect((await mk("e2e_src", "E2E Source", 13)).success).toBe(true);
  expect((await mk("e2e_dst", "E2E Target", 14)).success).toBe(true);
  await api("POST", "/lights/e2e_src/command", { power: false, transitionMs: 0 });
  await api("POST", "/lights/e2e_dst/command", { power: false, transitionMs: 0 });

  await signIn(page);
  await page.goto("/automations/new");
  await page.getByLabel("Name").fill("E2E follow");
  await page.getByRole("combobox", { name: "Trigger type" }).click();
  await page.getByRole("option", { name: "Light turns on/off" }).click();
  await page.getByRole("combobox", { name: "Light" }).first().click();
  await page.getByRole("option", { name: "E2E Source" }).click();
  // Action 1: turn on target at 100 %.
  const actionLight = page.getByRole("combobox", { name: "Light" }).nth(1);
  await actionLight.click();
  await page.getByRole("option", { name: "E2E Target" }).click();
  await expect(page.getByText(/When E2E Source turns on/)).toBeVisible();
  await page.getByRole("button", { name: "Save" }).click();
  await expect(page).toHaveURL(/\/automations\/e2e_follow$/);

  await api("POST", "/lights/e2e_src/command", { power: true, transitionMs: 0 });
  await expect
    .poll(async () => ((await api("GET", "/hardware/outputs/pca9685_0:14/readback")).data as { percent: number }).percent, { timeout: 5000 })
    .toBe(100);

  await page.goto("/automations");
  await expect(page.getByText("E2E follow")).toBeVisible();
  await page.getByRole("button", { name: "History of E2E follow" }).click();
  await expect(page.getByRole("dialog").getByText("OK").first()).toBeVisible();

  await api("DELETE", "/automations/e2e_follow");
  for (const id of ["e2e_src", "e2e_dst"]) await api("DELETE", `/lights/${id}`);
});
