// Real-hardware flow: create a light on a free PCA9685 output, control it
// from the UI, and verify the PCA9685 registers through read-back.
import { expect, test } from "@playwright/test";

import { apiSession, signIn } from "./helpers";

test.describe.configure({ mode: "serial" });

test("UI controls a real PCA9685 output end to end", async ({ page, request }) => {
  const api = await apiSession(request);
  await api("DELETE", "/lights/e2e_lamp");
  await api("DELETE", "/rooms/e2e_room");
  const room = await api("POST", "/rooms", { id: "e2e_room", name: "E2E Room" });
  expect(room.success).toBe(true);
  const light = await api("POST", "/lights", {
    id: "e2e_lamp",
    name: "E2E Lamp",
    type: "dimmer",
    roomId: "e2e_room",
    driver: { type: "pca9685", deviceId: "pca9685_0", channel: 15 },
    transitionMs: 0,
  });
  expect(light.success, JSON.stringify(light.error)).toBe(true);

  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await signIn(page);
  await expect(page.getByRole("img", { name: "Controller online" })).toBeVisible({ timeout: 15000 });

  await page.goto("/rooms/e2e_room");
  const toggle = page.getByRole("button", { name: /E2E Lamp: off/ });
  await toggle.click();
  await expect(page.getByRole("button", { name: /E2E Lamp: on/ })).toBeVisible();
  await expect
    .poll(async () => ((await api("GET", "/hardware/outputs/pca9685_0:15/readback")).data as { percent: number }).percent, { timeout: 5000 })
    .toBe(100);

  // Keyboard on the brightness slider -> throttled commands -> hardware.
  const slider = page.getByRole("slider", { name: "E2E Lamp brightness" });
  await slider.focus();
  await page.keyboard.press("End");
  for (let i = 0; i < 50; i++) await page.keyboard.press("ArrowLeft");
  await expect(slider).toHaveAttribute("aria-valuenow", "50");
  await expect
    .poll(async () => ((await api("GET", "/hardware/outputs/pca9685_0:15/readback")).data as { percent: number }).percent, { timeout: 5000 })
    .toBeCloseTo(18.4, 0); // perceptual curve: 50% -> 18.4% duty

  // State pushed over the WebSocket from another client shows up live.
  await api("POST", "/lights/e2e_lamp/command", { power: false });
  await expect(page.getByRole("button", { name: /E2E Lamp: off/ })).toBeVisible({ timeout: 3000 });

  expect(errors).toEqual([]);
  await api("DELETE", "/lights/e2e_lamp");
  await api("DELETE", "/rooms/e2e_room");
});
