// SVG sanitiser (DESIGN §9.2, AGENTS.md §10). Allowlist, DOMParser-based,
// no dependency. Runs before upload (the sanitised text is what the
// controller stores — and the firmware rejects anything unsafe on its own)
// and again before every render.
//
// Kept: shapes, paths, text, groups, gradients/patterns/clip/mask/markers,
// basic filters, <style> without @import / external url(), local <use>,
// raster data: images. Dropped: everything else, every on* attribute,
// every external reference.

export const MAX_SVG_BYTES = 128 * 1024;
export const MAX_SVG_ELEMENTS = 4000;

const SVG_NS = "http://www.w3.org/2000/svg";
const XLINK_NS = "http://www.w3.org/1999/xlink";

const ELEMENTS = new Set(
  [
    "svg", "g", "defs", "symbol", "use", "title", "desc", "metadata", "switch",
    "path", "rect", "circle", "ellipse", "line", "polyline", "polygon",
    "text", "tspan", "textPath",
    "linearGradient", "radialGradient", "stop", "pattern", "clipPath", "mask", "marker",
    "image", "style",
    "filter", "feGaussianBlur", "feOffset", "feBlend", "feColorMatrix", "feComposite", "feFlood", "feMerge", "feMergeNode", "feMorphology",
  ].map((e) => e.toLowerCase()),
);

const ATTRIBUTES = new Set(
  [
    "id", "class", "style", "transform", "viewBox", "preserveAspectRatio", "width", "height", "x", "y", "x1", "y1", "x2", "y2",
    "cx", "cy", "r", "rx", "ry", "d", "points", "pathLength", "fill", "fill-opacity", "fill-rule", "stroke", "stroke-width",
    "stroke-opacity", "stroke-linecap", "stroke-linejoin", "stroke-dasharray", "stroke-dashoffset", "stroke-miterlimit",
    "opacity", "color", "display", "visibility", "clip-path", "clip-rule", "mask", "filter", "marker-start", "marker-mid",
    "marker-end", "font-family", "font-size", "font-weight", "font-style", "text-anchor", "dominant-baseline", "letter-spacing",
    "dx", "dy", "rotate", "lengthAdjust", "textLength", "startOffset", "offset", "stop-color", "stop-opacity",
    "gradientUnits", "gradientTransform", "spreadMethod", "fx", "fy", "fr", "patternUnits", "patternContentUnits",
    "patternTransform", "clipPathUnits", "maskUnits", "maskContentUnits", "markerWidth", "markerHeight", "markerUnits", "refX",
    "refY", "orient", "href", "xlink:href", "xmlns", "xmlns:xlink", "version", "stdDeviation", "in", "in2", "result", "mode",
    "values", "type", "operator", "k1", "k2", "k3", "k4", "flood-color", "flood-opacity", "radius", "vector-effect",
    "shape-rendering", "paint-order", "mix-blend-mode", "isolation", "xml:space",
  ].map((a) => a.toLowerCase()),
);

const SAFE_DATA_IMAGE = /^data:image\/(png|jpeg|jpg|gif|webp)[;,]/i;
const BAD_VALUE = /(javascript|vbscript)\s*:|data:\s*(text|application)|expression\s*\(|-moz-binding|behavior\s*:/i;

export interface SanitizeResult {
  svg: string;
  viewBox: [number, number, number, number];
  elements: number;
  removed: string[]; // what was stripped (for the upload summary)
}

export class SvgRejected extends Error {}

function urlsLocal(value: string): boolean {
  const re = /url\(\s*(['"]?)\s*([^'")\s]*)/gi;
  let m: RegExpExecArray | null;
  while ((m = re.exec(value))) {
    if (!m[2].startsWith("#")) return false;
  }
  return true;
}

function cleanCss(css: string): string | null {
  if (/@import/i.test(css) || BAD_VALUE.test(css) || !urlsLocal(css)) return null;
  return css;
}

function parseViewBox(root: Element): [number, number, number, number] | null {
  const vb = root.getAttribute("viewBox");
  if (vb) {
    const n = vb.trim().split(/[\s,]+/).map(Number);
    if (n.length === 4 && n.every(Number.isFinite) && n[2] > 0 && n[3] > 0) return [n[0], n[1], n[2], n[3]];
  }
  const w = parseFloat(root.getAttribute("width") ?? "");
  const h = parseFloat(root.getAttribute("height") ?? "");
  return w > 0 && h > 0 ? [0, 0, w, h] : null;
}

/** Sanitise SVG text; throws SvgRejected when it cannot be made safe/usable. */
export function sanitizeSvg(text: string): SanitizeResult {
  if (new Blob([text]).size > MAX_SVG_BYTES) throw new SvgRejected("The file is larger than 128 KB. Simplify or minify the drawing.");
  if (/<!DOCTYPE|<!ENTITY/i.test(text)) throw new SvgRejected("DOCTYPE/ENTITY declarations are not allowed.");
  const doc = new DOMParser().parseFromString(text, "image/svg+xml");
  if (doc.getElementsByTagName("parsererror").length > 0) throw new SvgRejected("This is not a valid SVG file.");
  const root = doc.documentElement;
  if (root.localName.toLowerCase() !== "svg") throw new SvgRejected("The file's root element must be <svg>.");
  const viewBox = parseViewBox(root);
  if (!viewBox) throw new SvgRejected("The drawing needs a viewBox (or width and height).");

  const removed = new Set<string>();
  let elements = 0;
  const walk = (el: Element) => {
    for (const child of Array.from(el.children)) {
      const name = child.localName.toLowerCase();
      if (!ELEMENTS.has(name) || (child.namespaceURI !== SVG_NS && child.namespaceURI !== null)) {
        removed.add(`<${child.localName}>`);
        child.remove();
        continue;
      }
      walk(child);
    }
    elements++;
    for (const attr of Array.from(el.attributes)) {
      const n = attr.name.toLowerCase();
      const v = attr.value;
      const drop = (why: string) => {
        removed.add(why);
        el.removeAttributeNode(attr);
      };
      if (n.startsWith("on")) drop("event handlers");
      else if (!ATTRIBUTES.has(n) && !n.startsWith("aria-") && !n.startsWith("data-")) drop(`attribute ${attr.name}`);
      else if (n === "xmlns" || n === "xmlns:xlink") continue;
      else if (BAD_VALUE.test(v)) drop("script URLs");
      else if (n === "href" || n === "xlink:href") {
        const t = v.trim();
        if (!(t.startsWith("#") || (el.localName === "image" && SAFE_DATA_IMAGE.test(t)))) drop("external references");
      } else if (n === "style") {
        const css = cleanCss(v);
        if (css === null) drop("unsafe inline styles");
      } else if (!urlsLocal(v)) drop("external url()");
    }
    if (el.localName.toLowerCase() === "style") {
      const css = cleanCss(el.textContent ?? "");
      if (css === null) {
        removed.add("unsafe <style>");
        el.textContent = "";
      }
    }
    if (el.localName.toLowerCase() === "image" && !el.getAttribute("href") && !el.getAttributeNS(XLINK_NS, "href")) {
      removed.add("external images");
      el.remove();
    }
  };
  walk(root);
  if (elements > MAX_SVG_ELEMENTS) throw new SvgRejected(`The drawing has ${elements} elements (limit ${MAX_SVG_ELEMENTS}).`);

  // Comments / processing instructions are not needed for rendering.
  const strip = (n: Node) => {
    for (const c of Array.from(n.childNodes)) {
      if (c.nodeType === Node.COMMENT_NODE || c.nodeType === Node.PROCESSING_INSTRUCTION_NODE) c.parentNode?.removeChild(c);
      else strip(c);
    }
  };
  strip(doc);
  if (!root.getAttribute("viewBox")) root.setAttribute("viewBox", viewBox.join(" "));
  const svg = new XMLSerializer().serializeToString(root);
  if (new Blob([svg]).size > MAX_SVG_BYTES) throw new SvgRejected("The cleaned drawing is larger than 128 KB.");
  return { svg, viewBox, elements, removed: [...removed] };
}

/** Inner markup of a sanitised <svg> (rendered inside our own <svg>). */
export function innerMarkup(sanitized: string): string {
  const doc = new DOMParser().parseFromString(sanitized, "image/svg+xml");
  const s = new XMLSerializer();
  return Array.from(doc.documentElement.childNodes)
    .map((n) => s.serializeToString(n))
    .join("");
}
