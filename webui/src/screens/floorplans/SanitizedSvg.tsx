// The ONLY component allowed to use dangerouslySetInnerHTML (AGENTS.md §10).
// It always re-runs the sanitiser on its input before rendering, so callers
// cannot pass unsanitised markup through it by mistake.
import * as React from "react";

import { innerMarkup, sanitizeSvg } from "./sanitizeSvg";

export function SanitizedSvg({ svg, className }: { svg: string; className?: string }) {
  const html = React.useMemo(() => {
    try {
      return innerMarkup(sanitizeSvg(svg).svg);
    } catch {
      return "";
    }
  }, [svg]);
  // Rendered inside the caller's <svg> (same viewBox): a <g> of plain SVG.
  return <g className={className} aria-hidden="true" dangerouslySetInnerHTML={{ __html: html }} />;
}
