// Lumen web UI build config.
// - Tailwind v4 (checked against DESIGN §2 version policy: v4.3.x)
// - `/api` (REST) and `/api/v1/ws` (WebSocket) proxy to the controller,
//   target from env var so any device IP works (DESIGN §14.4).
import { defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import { fileURLToPath } from "node:url";

const controller = process.env.LUMEN_CONTROLLER_URL ?? "http://127.0.0.1:8080";

export default defineConfig({
  base: "/",
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: {
      "@": fileURLToPath(new URL("./src", import.meta.url)),
    },
  },
  server: {
    proxy: {
      // Single entry covers /api and /api/v1/ws; ws:true handles the upgrade.
      "/api": {
        target: controller,
        changeOrigin: true,
        ws: true,
      },
    },
  },
  build: {
    // No source maps in the firmware image (DESIGN §14.2).
    sourcemap: false,
  },
  test: {
    environment: "jsdom",
    include: ["src/**/*.test.{ts,tsx}"],
    setupFiles: ["src/test-setup.ts"],
  },
});
