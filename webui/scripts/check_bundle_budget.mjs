#!/usr/bin/env node
// Bundle budget check — DESIGN §14.1 (gzipped). Runs after `npm run build`.
//   initial JS = entry chunk + its modulepreloads (what the first paint needs)
//   total      = everything that lands in the LittleFS image (896 KB
//                partition; UI budget 500 KB so config/floor plans fit)
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
  { label: "Total UI image ≤ 500 KB", kb: files.reduce((s, f) => s + gz(f), 0), budget: 500 },
];

// Lazy route chunks (DESIGN §14.1): the chunk itself plus the chunks only it
// imports (shared chunks already loaded by other screens are not charged).
const assets = files.filter((f) => f.endsWith(".js"));
const chunk = (prefix) => assets.find((f) => f.split("/").pop().startsWith(prefix + "-"));
const importsOf = (f) =>
  [...readFileSync(f, "utf8").matchAll(/(?:from|import)\s*["']\.\/([^"']+\.js)["']/g)].map((m) => join(dirname(f), m[1]));
const importers = new Map();
for (const f of assets) for (const d of importsOf(f)) importers.set(d, [...(importers.get(d) ?? []), f]);
function routeCost(file) {
  const exclusive = importsOf(file).filter((d) => !initialRefs.includes(d) && (importers.get(d) ?? []).every((i) => i === file));
  return gz(file) + exclusive.reduce((s, d) => s + gz(d), 0);
}
for (const [prefix, budget, label] of [["FloorPlanViewer", 40, "Floor-plan viewer chunk ≤ 40 KB"], ["FloorPlanEditor", 90, "Floor-plan editor chunk (Konva) ≤ 90 KB"]]) {
  const f = chunk(prefix);
  checks.push(f ? { label, kb: routeCost(f), budget } : { label: `${label} (chunk missing)`, kb: Infinity, budget });
}
// Konva must only ever load with the editor route (AGENTS.md §9/§10).
const konvaOutsideEditor = assets.filter((f) => !f.includes("FloorPlanEditor-") && /Konva\.(Stage|Layer)|konva/i.test(readFileSync(f, "utf8")) && /_validateAdd|getStage\(\)/.test(readFileSync(f, "utf8")));
checks.push({ label: "Konva only in the editor chunk", kb: konvaOutsideEditor.length, budget: 0 });

let failed = 0;
for (const c of checks) {
  const ok = c.kb <= c.budget;
  if (!ok) failed++;
  console.log(`${ok ? "ok  " : "FAIL"} ${c.label} — ${c.kb.toFixed(1)} KB gz`);
}
process.exit(failed ? 1 : 0);
