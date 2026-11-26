// Mandatory sanitiser suite (DESIGN §16, AGENTS.md §11): malicious samples
// must come out harmless or be rejected.
import { describe, expect, it } from "vitest";

import { innerMarkup, MAX_SVG_BYTES, sanitizeSvg, SvgRejected } from "./sanitizeSvg";

const wrap = (body: string, attrs = "") => `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 100 100"${attrs}>${body}</svg>`;

function clean(body: string, attrs = "") {
  return sanitizeSvg(wrap(body, attrs));
}

const DANGEROUS = /<script|onload|onclick|onerror|onmouse|javascript:|vbscript:|foreignObject|<iframe|<embed|<object|@import|http:\/\/evil|https:\/\/evil|<a[\s>]|<set|<animate|data:text|expression\(/i;

describe("sanitizeSvg — malicious samples", () => {
  const samples: [string, string, string?][] = [
    ["script element", `<script>alert(1)</script><rect width="1" height="1"/>`],
    ["script in CDATA", `<script><![CDATA[alert(1)]]></script>`],
    ["onload on root", ``, ` onload="alert(1)"`],
    ["event handler on child", `<rect onclick="alert(1)" onmouseover="x()" width="5" height="5"/>`],
    ["javascript: href on <a>", `<a href="javascript:alert(1)"><text>x</text></a>`],
    ["javascript: in use", `<use href="javascript:alert(1)"/>`],
    ["obfuscated javascript:", `<use xlink:href=" JaVaScRiPt:alert(1)"/>`],
    ["external use", `<use href="http://evil/sprite.svg#a"/>`],
    ["external image", `<image href="https://evil/track.png" width="5" height="5"/>`],
    ["svg data image", `<image href="data:image/svg+xml;base64,PHN2Zz4=" width="5" height="5"/>`],
    ["foreignObject", `<foreignObject><div xmlns="http://www.w3.org/1999/xhtml" onclick="x()">x</div></foreignObject>`],
    ["iframe/embed/object", `<iframe src="http://evil"/><embed src="http://evil"/><object data="http://evil"/>`],
    ["@import in style", `<style>@import url(http://evil/x.css); rect{fill:red}</style>`],
    ["external url in style", `<style>rect{fill:url(http://evil/p)}</style>`],
    ["external url in style attr", `<rect style="fill:url('https://evil/p')" width="1" height="1"/>`],
    ["external url in fill", `<rect fill="url(http://evil/#g)" width="1" height="1"/>`],
    ["expression()", `<rect style="width:expression(alert(1))"/>`],
    ["animate rewriting href", `<a href="#x"><animate attributeName="href" values="javascript:alert(1)"/></a>`],
    ["set element", `<set attributeName="onmouseover" to="alert(1)"/>`],
    ["data:text href", `<image href="data:text/html,&lt;script&gt;alert(1)&lt;/script&gt;"/>`],
  ];
  for (const [name, body, attrs] of samples) {
    it(`neutralises: ${name}`, () => {
      const r = clean(body, attrs);
      expect(r.svg).not.toMatch(DANGEROUS);
      expect(innerMarkup(r.svg)).not.toMatch(DANGEROUS);
      expect(r.removed.length).toBeGreaterThan(0);
    });
  }

  it("rejects DOCTYPE / ENTITY (XXE, entity bombs)", () => {
    expect(() => sanitizeSvg(`<!DOCTYPE svg [<!ENTITY a "aaaa">]>${wrap("&a;")}`)).toThrow(SvgRejected);
  });
  it("rejects oversized files", () => {
    expect(() => sanitizeSvg(wrap(`<desc>${"a".repeat(MAX_SVG_BYTES)}</desc>`))).toThrow(/128 KB/);
  });
  it("rejects element-count bombs", () => {
    expect(() => sanitizeSvg(wrap("<g/>".repeat(4100)))).toThrow(/elements/);
  });
  it("rejects non-SVG and invalid XML", () => {
    expect(() => sanitizeSvg("<html><body/></html>")).toThrow(SvgRejected);
    expect(() => sanitizeSvg("<svg viewBox='0 0 1 1'><g></svg>")).toThrow(SvgRejected);
    expect(() => sanitizeSvg("<svg xmlns='http://www.w3.org/2000/svg'><g/></svg>")).toThrow(/viewBox/);
  });
});

describe("sanitizeSvg — keeps real drawings", () => {
  it("keeps shapes, text, gradients, local references and raster data images", () => {
    const r = clean(
      `<defs><linearGradient id="g"><stop offset="0" stop-color="#fff"/></linearGradient><style>.w{stroke:#333;fill:url(#g)}</style></defs>` +
        `<g id="walls" class="w"><rect x="1" y="1" width="98" height="98" fill="url('#g')"/><path d="M0 0 L10 10"/>` +
        `<text x="5" y="20">Living &amp; dining</text><use href="#walls"/>` +
        `<image href="data:image/png;base64,iVBORw0KGgo=" width="4" height="4"/></g>`,
    );
    expect(r.removed).toEqual([]);
    expect(r.svg).toContain("linearGradient");
    expect(r.svg).toContain('href="#walls"');
    expect(r.svg).toContain("Living &amp; dining");
    expect(r.svg).toContain("data:image/png");
    expect(r.viewBox).toEqual([0, 0, 100, 100]);
  });
  it("derives a viewBox from width/height and drops editor metadata", () => {
    const r = sanitizeSvg(
      `<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="640" height="480" inkscape:version="1.3"><inkscape:grid/><rect width="10" height="10"/></svg>`,
    );
    expect(r.viewBox).toEqual([0, 0, 640, 480]);
    expect(r.svg).toContain('viewBox="0 0 640 480"');
    expect(r.svg).not.toMatch(/inkscape:grid|inkscape:version/);
  });
  it("is idempotent (render-time pass over stored output changes nothing)", () => {
    const once = clean(`<g><rect width="5" height="5" onclick="x()"/></g>`).svg;
    const twice = sanitizeSvg(once);
    expect(twice.svg).toBe(once);
    expect(twice.removed).toEqual([]);
  });
});
