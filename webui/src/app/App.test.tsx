import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, render, screen } from "@testing-library/react";

import App from "./App";

function envelope(status: number, body: unknown) {
  return Promise.resolve(new Response(JSON.stringify(body), { status, headers: { "Content-Type": "application/json" } }));
}

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
  window.history.pushState({}, "", "/");
});

describe("App auth gate", () => {
  it("redirects to sign-in when there is no session", async () => {
    vi.stubGlobal("fetch", vi.fn(() => envelope(401, { success: false, data: null, error: { code: "UNAUTHORIZED", message: "Not signed in" } })));
    render(<App />);
    expect(await screen.findByRole("heading", { name: /sign in to lumen/i })).toBeTruthy();
    expect(window.location.pathname).toBe("/login");
  });

  it("forces the password change before anything else", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(() => envelope(200, { success: true, data: { username: "admin", forcePasswordChange: true, csrfToken: "c" }, error: null })),
    );
    render(<App />);
    expect(await screen.findByRole("heading", { name: /choose a new password/i })).toBeTruthy();
    expect(window.location.pathname).toBe("/setup/password");
  });
});
