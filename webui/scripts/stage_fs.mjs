#!/usr/bin/env node
// Stage the built UI into ../data/web for the LittleFS image
// (`pio run -t buildfs` / `uploadfs`). Every asset is stored gzip-only
// (Content-Encoding: gzip on the device); the firmware's config documents
// live under /config and are never touched by the image.
import { mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { gzipSync } from "node:zlib";
import { dirname, join, relative } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const dist = join(root, "dist");
const out = join(root, "..", "data", "web");

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

function walk(dir) {
  return readdirSync(dir).flatMap((f) => {
    const p = join(dir, f);
    return statSync(p).isDirectory() ? walk(p) : [p];
  });
}

let total = 0;
for (const file of walk(dist)) {
  const rel = relative(dist, file);
  const target = join(out, rel + ".gz");
  mkdirSync(dirname(target), { recursive: true });
  const data = gzipSync(readFileSync(file), { level: 9 });
  writeFileSync(target, data);
  total += data.length;
}
console.log(`stage_fs: ${(total / 1024).toFixed(1)} KB gzipped -> ${relative(join(root, ".."), out)}`);
