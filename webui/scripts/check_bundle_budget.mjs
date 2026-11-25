#!/usr/bin/env node
// Bundle budget check — DESIGN §14.1 (gzipped). Runs after `npm run build`.
//   initial JS = entry chunk + its modulepreloads (what the first paint needs)
//   total      = everything that lands in the LittleFS image (640 KB
//                partition; UI budget 450 KB so config/floor plans fit)
import { readdirSync, readFileSync, statSync } from "node:fs";
import { gzipSync } from "node:zlib";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const dist = join(dirname(fileURLToPath(import.meta.url)), "..", "dist");

function walk(dir) {
  return readdirSync(dir).flatMap((f) => {
    const p = join(dir, f);
    return statSync(p).isDirectory() ? walk(p) : [p];
  });
}

let files;
let html;
try {
  files = walk(dist);
  html = readFileSync(join(dist, "index.html"), "utf8");
} catch {
  console.error(`check_bundle_budget: no build output at ${dist} — run npm run build first`);
  process.exit(1);
}

const gz = (p) => gzipSync(readFileSync(p), { level: 9 }).length / 1024;
const initialRefs = [...html.matchAll(/(?:src|href)="\/(assets\/[^"]+\.js)"/g)].map((m) => join(dist, m[1]));
const css = files.filter((f) => f.endsWith(".css"));

const checks = [
  { label: "Initial JS (entry + preloads) ≤ 160 KB", kb: initialRefs.reduce((s, f) => s + gz(f), 0), budget: 160 },
  { label: "CSS ≤ 25 KB", kb: css.reduce((s, f) => s + gz(f), 0), budget: 25 },
  { label: "Total UI image ≤ 450 KB", kb: files.reduce((s, f) => s + gz(f), 0), budget: 450 },
];

let failed = 0;
for (const c of checks) {
  const ok = c.kb <= c.budget;
  if (!ok) failed++;
  console.log(`${ok ? "ok  " : "FAIL"} ${c.label} — ${c.kb.toFixed(1)} KB gz`);
}
process.exit(failed ? 1 : 0);
