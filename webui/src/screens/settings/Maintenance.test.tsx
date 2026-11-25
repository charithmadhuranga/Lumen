import { afterEach, describe, expect, it, vi } from "vitest";
import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";

import { FirmwareSection } from "./Maintenance";

const STATUS = {
  state: "idle",
  received: 0,
  total: 0,
  error: "",
  newVersion: "",
  running: { partition: "ota_1", version: "0.1.1", state: "valid" },
  other: { partition: "ota_0", version: "0.1.0", state: "valid" },
  slotSize: 1703936,
  rollbackPossible: true,
};

const ok = (data: unknown) =>
  Promise.resolve(new Response(JSON.stringify({ success: true, data, error: null }), { status: 200, headers: { "Content-Type": "application/json" } }));

/** Minimal XMLHttpRequest double answering one upload. */
function stubXhr(status: number, body: unknown) {
  const sent: { url?: string; headers: Record<string, string>; body?: unknown } = { headers: {} };
  class FakeXhr {
    upload: { onprogress: ((e: { lengthComputable: boolean; loaded: number; total: number }) => void) | null } = { onprogress: null };
    status = 0;
    responseText = "";
    timeout = 0;
    withCredentials = false;
    onload: (() => void) | null = null;
    onerror: (() => void) | null = null;
    ontimeout: (() => void) | null = null;
    open(_m: string, url: string) {
      sent.url = url;
    }
    setRequestHeader(k: string, v: string) {
      sent.headers[k] = v;
    }
    getResponseHeader() {
      return null;
    }
    send(b: unknown) {
      sent.body = b;
      this.upload.onprogress?.({ lengthComputable: true, loaded: 50, total: 100 });
      this.status = status;
      this.responseText = JSON.stringify(body);
      setTimeout(() => this.onload?.(), 0);
    }
  }
  vi.stubGlobal("XMLHttpRequest", FakeXhr);
  return sent;
}

function renderFw() {
  vi.stubGlobal("fetch", vi.fn(() => ok(STATUS)));
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  render(
    <QueryClientProvider client={qc}>
      <FirmwareSection />
    </QueryClientProvider>,
  );
}

afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});

describe("FirmwareSection", () => {
  it("shows both slots and needs a file before installing", async () => {
    renderFw();
    expect(await screen.findByText(/0\.1\.1 · slot ota_1 · confirmed/)).toBeTruthy();
    expect(screen.getByText(/0\.1\.0 · confirmed/)).toBeTruthy();
    expect((screen.getByRole("button", { name: /install firmware/i }) as HTMLButtonElement).disabled).toBe(true);
  });

  it("streams the file as octet-stream and reports a rejected image", async () => {
    const sent = stubXhr(400, { success: false, data: null, error: { code: "INVALID_FIRMWARE", message: "The image is corrupted" } });
    renderFw();
    await screen.findByText(/slot ota_1/);
    const file = new File([new Uint8Array([0xe9, 1, 2])], "firmware.bin");
    fireEvent.change(screen.getByLabelText(/choose firmware/i), { target: { files: [file] } });
    fireEvent.click(screen.getByRole("button", { name: /install firmware/i }));
    fireEvent.click(await screen.findByRole("button", { name: /install and restart/i }));
    await waitFor(() => expect(sent.url).toBe("/api/v1/ota/upload"));
    expect(sent.headers["Content-Type"]).toBe("application/octet-stream");
    expect(sent.body).toBe(file);
    // Failed install: the button becomes usable again (no "restarting" state).
    await waitFor(() => expect((screen.getByRole("button", { name: /install firmware/i }) as HTMLButtonElement).disabled).toBe(false));
    expect(screen.queryByRole("progressbar")).toBeNull();
  });

  it("blocks a new update while the running firmware is still confirming itself", async () => {
    vi.stubGlobal("fetch", vi.fn(() => ok({ ...STATUS, running: { ...STATUS.running, state: "pending_verify" } })));
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
    render(
      <QueryClientProvider client={qc}>
        <FirmwareSection />
      </QueryClientProvider>,
    );
    expect(await screen.findByText(/still confirming itself/)).toBeTruthy();
  });
});
