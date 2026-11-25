import { expect, type APIRequestContext, type Page } from "@playwright/test";

export const PASSWORD = process.env.LUMEN_PASSWORD ?? "";

export async function signIn(page: Page) {
  if (!PASSWORD) throw new Error("Set LUMEN_PASSWORD to the controller's admin password");
  await page.goto("/login");
  await page.getByLabel("Username").fill("admin");
  await page.getByLabel("Password").fill(PASSWORD);
  await page.getByRole("button", { name: "Sign in" }).click();
  await expect(page).toHaveURL(/\/$/);
}

/** Authenticated API session for setup/verification (cookie + CSRF). */
export async function apiSession(request: APIRequestContext) {
  const res = await request.post("/api/v1/auth/login", { data: { username: "admin", password: PASSWORD } });
  const body = await res.json();
  if (!body.success) throw new Error(`login failed: ${JSON.stringify(body.error)}`);
  const csrf: string = body.data.csrfToken;
  const call = async (method: "GET" | "POST" | "PUT" | "DELETE", path: string, data?: unknown) => {
    const r = await request.fetch(`/api/v1${path}`, { method, data: data ?? (method === "GET" ? undefined : {}), headers: method === "GET" ? {} : { "X-CSRF-Token": csrf } });
    const j = await r.json();
    return j as { success: boolean; data: unknown; error?: { code: string; message: string } };
  };
  return call;
}
