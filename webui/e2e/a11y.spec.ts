// Accessibility + layout: axe (no serious/critical violations) on the main
// routes in light and dark themes, and no horizontal scroll on small screens.
import AxeBuilder from "@axe-core/playwright";
import { expect, test } from "@playwright/test";

import { signIn } from "./helpers";

const ROUTES = ["/", "/rooms", "/scenes", "/devices", "/hardware", "/network", "/settings"];

test("login page is accessible", async ({ page }) => {
  await page.goto("/login");
  const r = await new AxeBuilder({ page }).analyze();
  const bad = r.violations.filter((v) => v.impact === "serious" || v.impact === "critical");
  expect(bad.map((v) => `${v.id}: ${v.nodes[0]?.target}`)).toEqual([]);
});

for (const theme of ["light", "dark"] as const) {
  test(`main routes have no serious axe violations (${theme})`, async ({ page }) => {
    await page.addInitScript((t) => window.localStorage.setItem("lumen-theme", t), theme);
    await signIn(page);
    for (const route of ROUTES) {
      await page.goto(route);
      await page.waitForLoadState("networkidle");
      const r = await new AxeBuilder({ page }).analyze();
      const bad = r.violations.filter((v) => v.impact === "serious" || v.impact === "critical");
      expect(bad.map((v) => `${route} ${v.id}: ${v.help} @ ${v.nodes[0]?.target}`)).toEqual([]);
      const overflow = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
      expect(overflow, `${route} scrolls horizontally`).toBeLessThanOrEqual(1);
    }
  });
}
