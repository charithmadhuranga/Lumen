import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";

import App from "./App";
import { Providers } from "./providers";

// Phase 0 render smoke: providers (theme, Motion, tooltips, toaster) must
// mount without throwing and the landing must actually render.
describe("App", () => {
  it("renders the Phase 0 landing inside providers", () => {
    const html = renderToStaticMarkup(
      <Providers>
        <App />
      </Providers>,
    );
    expect(html).toContain("Lumen");
    expect(html).toContain("Phase 0");
    expect(html).toContain("Theme:");
  });
});
