#!/usr/bin/env node
// Stage the built UI into ../data/web/web.pack for the LittleFS image.
//
// LittleFS spends a 4 KB block plus metadata per file, so ~40 small assets
// waste ~150 KB of a 640 KB partition. All assets are gzipped and packed
// into ONE file the firmware serves by offset (src/web/web_pack.cpp):
//
//   "LWPK" | u32 version=1 | u32 count | count x { u16 pathLen, path (no
//   leading '/'), u32 offset, u32 length } | data (gzip streams)
// Little-endian. Offsets are absolute from the start of the file.
// Replacing web.pack is also how the UI is updated without touching /config.
import { mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { gzipSync } from "node:zlib";
import { dirname, join, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const dist = join(root, "dist");
const out = join(root, "..", "data", "web");

function walk(dir) {
  return readdirSync(dir).flatMap((f) => {
    const p = join(dir, f);
    return statSync(p).isDirectory() ? walk(p) : [p];
  });
}

const entries = walk(dist)
  .sort()
  .map((file) => ({ path: relative(dist, file).split(sep).join("/"), data: gzipSync(readFileSync(file), { level: 9 }) }));

let headerSize = 12;
for (const e of entries) headerSize += 2 + Buffer.byteLength(e.path) + 8;
let offset = headerSize;
const parts = [];
const header = Buffer.alloc(headerSize);
header.write("LWPK", 0, "ascii");
header.writeUInt32LE(1, 4);
header.writeUInt32LE(entries.length, 8);
let p = 12;
for (const e of entries) {
  const len = Buffer.byteLength(e.path);
  header.writeUInt16LE(len, p);
  header.write(e.path, p + 2, "utf8");
  header.writeUInt32LE(offset, p + 2 + len);
  header.writeUInt32LE(e.data.length, p + 6 + len);
  p += 10 + len;
  offset += e.data.length;
  parts.push(e.data);
}

rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
const pack = Buffer.concat([header, ...parts]);
writeFileSync(join(out, "web.pack"), pack);
console.log(`stage_fs: ${entries.length} assets, ${(pack.length / 1024).toFixed(1)} KB -> data/web/web.pack`);
