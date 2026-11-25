// End-to-end tests run against a REAL controller (DESIGN §16):
//   LUMEN_URL=http://<device> LUMEN_PASSWORD=<admin password> npm run test:e2e
// They create test lights/rooms with an "e2e_" prefix and remove them again.
import { defineConfig, devices } from "@playwright/test";

export default defineConfig({
  testDir: "e2e",
  timeout: 60_000,
  retries: 0,
  workers: 1, // one controller: tests share hardware state
  reporter: [["list"]],
  use: {
    baseURL: process.env.LUMEN_URL ?? "http://lumen.local",
    trace: "retain-on-failure",
    launchOptions: process.env.PW_CHROMIUM ? { executablePath: process.env.PW_CHROMIUM } : {},
  },
  projects: [
    { name: "desktop", use: { ...devices["Desktop Chrome"] } },
    { name: "mobile", use: { ...devices["Pixel 7"], viewport: { width: 360, height: 780 } } },
  ],
});
