// Real-device floor-plan flow: upload an SVG (with a script that must be
// stripped), place a light in Edit mode (Konva), save, then Control mode:
// the glow follows the light's actual output (changed from another client),
// tapping it drives the PCA9685, axe passes in both modes, and Konva is only
// downloaded on the editor route.
import AxeBuilder from "@axe-core/playwright";
import { expect, test } from "@playwright/test";

import { apiSession, signIn } from "./helpers";

test.describe.configure({ mode: "serial" });

const SVG = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 300">
  <rect x="5" y="5" width="390" height="290" fill="none" stroke="#555" stroke-width="4"/>
  <text x="20" y="40" font-size="20">E2E room</text>
  <script>window.__pwned = true</script>
  <circle cx="10" cy="10" r="3" onload="window.__pwned = true"/>
</svg>`;

test("floor plan: upload, place, save, control real hardware", async ({ page, request }) => {
  const api = await apiSession(request);
  await api("DELETE", "/floorplans/e2e-plan");
  await api("DELETE", "/lights/e2e_fp");
  const light = await api("POST", "/lights", { id: "e2e_fp", name: "E2E Glow", type: "dimmer", driver: { type: "pca9685", deviceId: "pca9685_0", channel: 13 }, transitionMs: 0 });
  expect(light.success, JSON.stringify(light.error)).toBe(true);
  const plan = await api("POST", "/floorplans", { id: "e2e-plan", name: "E2E plan" });
  expect(plan.success, JSON.stringify(plan.error)).toBe(true);

  const konvaRequests: string[] = [];
  page.on("request", (r) => r.url().includes("FloorPlanEditor") && konvaRequests.push(r.url()));
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await signIn(page);

  // Edit mode: upload the drawing.
  await page.goto("/floorplans/e2e-plan/edit");
  await expect(page.getByText("Editing", { exact: true })).toBeVisible();
  await page.getByLabel("SVG drawing").first().setInputFiles({ name: "plan.svg", mimeType: "image/svg+xml", buffer: Buffer.from(SVG) });
  await expect(page.getByText("Drawing uploaded")).toBeVisible({ timeout: 10000 });
  const stored = await request.get("/api/v1/floorplans/e2e-plan/svg");
  const storedText = await stored.text();
  expect(storedText).not.toMatch(/<script|onload/);
  expect(storedText).toContain("E2E room");
  expect(konvaRequests.length).toBeGreaterThan(0);

  // Place the light (added at the drawing centre) and save.
  await page.getByRole("button", { name: "E2E Glow" }).click();
  await page.getByRole("button", { name: "Save" }).click();
  await expect(page.getByText("Floor plan saved")).toBeVisible();
  const saved = (await api("GET", "/floorplans/e2e-plan")).data as { lights: { lightId: string; x: number; y: number }[] };
  const pin = saved.lights.find((l) => l.lightId === "e2e_fp")!;
  expect(pin.x).toBeCloseTo(200, 0); // viewBox coordinates
  expect(pin.y).toBeCloseTo(150, 0);
  const editAxe = await new AxeBuilder({ page }).analyze();
  expect(editAxe.violations.filter((v) => v.impact === "serious" || v.impact === "critical").map((v) => v.id)).toEqual([]);

  // Control mode in a fresh page load: Konva must not be fetched.
  konvaRequests.length = 0;
  await page.goto("/floorplans/e2e-plan");
  await expect(page.getByRole("group", { name: "Floor plan E2E plan" })).toBeVisible();
  expect(konvaRequests).toEqual([]);
  expect(await page.evaluate(() => (window as unknown as { __pwned?: boolean }).__pwned)).toBeUndefined();
  const glow = page.getByTestId("glow-e2e_fp");
  await expect(glow).toHaveAttribute("data-on", "false");

  // Another client turns the light on: the glow follows the reported output.
  await api("POST", "/lights/e2e_fp/command", { power: true, brightness: 100, transitionMs: 0 });
  await expect(glow).toHaveAttribute("data-on", "true", { timeout: 5000 });

  // Tapping the light on the plan switches the real output off.
  await page.getByRole("switch", { name: "E2E Glow", exact: true }).first().click();
  await expect.poll(async () => ((await api("GET", "/hardware/outputs/pca9685_0:13/readback")).data as { percent: number }).percent, { timeout: 5000 }).toBe(0);
  await expect(glow).toHaveAttribute("data-on", "false", { timeout: 5000 });
  const ctlAxe = await new AxeBuilder({ page }).analyze();
  expect(ctlAxe.violations.filter((v) => v.impact === "serious" || v.impact === "critical").map((v) => `${v.id} ${v.nodes[0]?.target}`)).toEqual([]);

  expect(errors).toEqual([]);
  await api("DELETE", "/floorplans/e2e-plan");
  await api("DELETE", "/lights/e2e_fp");
});
