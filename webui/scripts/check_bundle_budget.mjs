#!/usr/bin/env node
// Bundle budget check — DESIGN §14.1 (gzipped), enforced from Phase 0 on
// the initial shell; per-chunk budgets (konva, reactflow, recharts) are
// added as those phases land and are marked "deferred" until then.
//
// Runs after `npm run build`, from CI and locally:
//   node scripts/check_bundle_budget.mjs

import { readdirSync, readFileSync, statSync } from "node:fs";
import { gzipSync } from "node:zlib";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const dist = join(dirname(fileURLToPath(import.meta.url)), "..", "dist");

/** @type {Array<{ label: string, match: (f: string) => boolean, budgetKB: number, deferred?: boolean }>} */
const budgets = [
  {
    label: "Initial JS (shell) — DESIGN §14.1 ≤ 150 KB",
    match: (f) => f.endsWith(".js"),
    budgetKB: 150,
  },
  {
    label: "CSS — DESIGN §14.1 ≤ 25 KB",
    match: (f) => f.endsWith(".css"),
    budgetKB: 25,
  },
];

// Per-chunk budgets deferred until Phase 6+ code splitting exists:
// floor-plan viewer ≤ 40 KB, konva editor ≤ 90 KB, reactflow ≤ 100 KB,
// recharts ≤ 90 KB, gsap ≤ 30 KB, total ≤ 1.0 MB (LittleFS partition).

let files;
try {
  files = readdirSync(join(dist, "assets")).filter((f) => statSync(join(dist, "assets", f)).isFile());
} catch {
  console.error(`check_bundle_budget: no build output at ${dist} — run npm run build first`);
  process.exit(1);
}

let failed = 0;

for (const budget of budgets) {
  const hits = files.filter((f) => budget.match(f));
  const totalKB =
    hits.reduce((sum, f) => sum + gzipSync(readFileSync(join(dist, "assets", f))).length, 0) / 1024;
  const ok = totalKB <= budget.budgetKB;
  if (!ok) failed += 1;
  console.log(
    `${ok ? "ok  " : "FAIL"} ${budget.label} — ${hits.length} file(s), ${totalKB.toFixed(1)} KB gz`,
  );
}

process.exit(failed === 0 ? 0 : 1);
