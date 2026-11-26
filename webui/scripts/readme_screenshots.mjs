// Screenshots for the repository README (docs/images/), taken from a REAL
// controller so they show the actual UI:
//   LUMEN_URL=http://<controller> LUMEN_PASSWORD=<admin password> node scripts/readme_screenshots.mjs
// Optional: LUMEN_PLAN=<floor plan id> (default "readme-demo"),
// LUMEN_AUTOMATION=<automation id> (default "evening_lights").
// Avoid screens that show network names or tokens (Network, Integrations).
import { mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "@playwright/test";

const base = process.env.LUMEN_URL;
const password = process.env.LUMEN_PASSWORD;
if (!base || !password) throw new Error("Set LUMEN_URL and LUMEN_PASSWORD");
const plan = process.env.LUMEN_PLAN ?? "readme-demo";
const automation = process.env.LUMEN_AUTOMATION ?? "evening_lights";
const out = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "docs", "images");
mkdirSync(out, { recursive: true });

const browser = await chromium.launch(process.env.PW_CHROMIUM ? { executablePath: process.env.PW_CHROMIUM } : {});

async function session(colorScheme, viewport, isMobile = false) {
  // Scale so desktop shots are 1600 px and phone shots 520 px wide.
  const deviceScaleFactor = isMobile ? 520 / viewport.width : 1600 / viewport.width;
  const ctx = await browser.newContext({ baseURL: base, colorScheme, viewport, deviceScaleFactor, isMobile, hasTouch: isMobile, reducedMotion: "reduce" });
  const page = await ctx.newPage();
  await page.goto("/login");
  await page.getByLabel("Username").fill("admin");
  await page.getByLabel("Password").fill(password);
  await page.getByRole("button", { name: "Sign in" }).click();
  await page.waitForURL((u) => u.pathname === "/");
  return { ctx, page };
}

async function shot(page, path, name, wait) {
  await page.goto(path);
  await page.waitForLoadState("networkidle");
  if (wait) await wait(page);
  await page.waitForTimeout(800); // live state + fonts settle
  await page.screenshot({ path: join(out, name) });
  console.log("wrote", name);
}

const desktop = { width: 1440, height: 900 };
for (const scheme of ["light", "dark"]) {
  const { ctx, page } = await session(scheme, desktop);
  await shot(page, "/", `overview-${scheme}.png`);
  if (scheme === "dark") {
    await shot(page, `/floorplans/${plan}`, "floorplan-control.png", (p) => p.getByRole("group").first().waitFor());
    await shot(page, `/floorplans/${plan}/edit`, "floorplan-editor.png", (p) => p.locator(".konvajs-content").waitFor());
    await shot(page, `/automations/${automation}`, "automation-editor.png", async (p) => {
      await p.getByRole("tab", { name: "Advanced" }).click();
      await p.locator(".react-flow").waitFor();
    });
    await shot(page, "/hardware", "hardware.png");
  }
  await ctx.close();
}
{
  const { ctx, page } = await session("dark", { width: 390, height: 844 }, true);
  await shot(page, "/", "mobile-overview.png");
  await shot(page, `/floorplans/${plan}`, "mobile-floorplan.png", (p) => p.getByRole("group").first().waitFor());
  await ctx.close();
}
await browser.close();
