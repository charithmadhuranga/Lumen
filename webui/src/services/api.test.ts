import { afterEach, describe, expect, it, vi } from "vitest";

import { api, ApiError, setCsrfToken, setUnauthorizedHandler } from "@/services/api";

function respond(status: number, body: unknown) {
  return vi.fn<(url: string, init?: RequestInit) => Promise<Response>>(() =>
    Promise.resolve(new Response(JSON.stringify(body), { status, headers: { "Content-Type": "application/json" } })),
  );
}

afterEach(() => {
  vi.unstubAllGlobals();
  setCsrfToken(null);
  setUnauthorizedHandler(null);
});

describe("api client", () => {
  it("unwraps the envelope and validates data", async () => {
    vi.stubGlobal("fetch", respond(200, { success: true, data: { username: "admin", forcePasswordChange: false, csrfToken: "x" }, error: null }));
    await expect(api.session()).resolves.toEqual({ username: "admin", forcePasswordChange: false, csrfToken: "x" });
  });

  it("throws a typed ApiError on success:false", async () => {
    vi.stubGlobal("fetch", respond(400, { success: false, data: null, error: { code: "INVALID_CONFIGURATION", message: "bad" } }));
    const err = await api.rooms().catch((e: unknown) => e);
    expect(err).toBeInstanceOf(ApiError);
    expect((err as ApiError).code).toBe("INVALID_CONFIGURATION");
    expect((err as ApiError).status).toBe(400);
  });

  it("rejects responses that do not match the schema", async () => {
    vi.stubGlobal("fetch", respond(200, { success: true, data: [{ id: 1 }], error: null }));
    await expect(api.rooms()).rejects.toMatchObject({ code: "INVALID_RESPONSE" });
  });

  it("sends the CSRF header on mutations only", async () => {
    const f = respond(202, { success: true, data: { accepted: true }, error: null });
    vi.stubGlobal("fetch", f);
    setCsrfToken("token123");
    await api.commandLight("a", { power: true });
    const init = f.mock.calls[0][1] as RequestInit;
    expect((init.headers as Record<string, string>)["X-CSRF-Token"]).toBe("token123");
    expect(init.credentials).toBe("same-origin");
    const g = respond(200, { success: true, data: [], error: null });
    vi.stubGlobal("fetch", g);
    await api.rooms();
    expect((g.mock.calls[0][1] as RequestInit).headers).not.toHaveProperty("X-CSRF-Token");
  });

  it("calls the unauthorized handler on 401", async () => {
    vi.stubGlobal("fetch", respond(401, { success: false, data: null, error: { code: "UNAUTHORIZED", message: "x" } }));
    const handler = vi.fn();
    setUnauthorizedHandler(handler);
    await expect(api.lights()).rejects.toBeInstanceOf(ApiError);
    expect(handler).toHaveBeenCalledOnce();
  });

  it("does not treat a failed login as session expiry", async () => {
    vi.stubGlobal("fetch", respond(401, { success: false, data: null, error: { code: "INVALID_CREDENTIALS", message: "x" } }));
    const handler = vi.fn();
    setUnauthorizedHandler(handler);
    await expect(api.login("a", "b")).rejects.toMatchObject({ code: "INVALID_CREDENTIALS" });
    expect(handler).not.toHaveBeenCalled();
  });
});
