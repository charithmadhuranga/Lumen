#!/usr/bin/env node
// Lumen Pro protocol contract check (.opencode/plans/pro/p00-foundation.md).
//
// 1. Every protocol/schemas/*.schema.json compiles against draft 2020-12.
// 2. Every protocol/fixtures/<schema>/valid/*.json validates.
// 3. Every protocol/fixtures/<schema>/invalid/*.json FAILS validation.
// 4. Every schema except common has at least one valid and one invalid fixture.
//
// Exit 0 when all pass; otherwise a per-file report and exit 1.

import { existsSync, readFileSync, readdirSync } from "node:fs";
import { basename, dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import Ajv2020 from "ajv/dist/2020.js";
import addFormats from "ajv-formats";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const schemasDir = join(root, "protocol", "schemas");
const fixturesDir = join(root, "protocol", "fixtures");

// allowUnionTypes: state.effect is ["string","null"]. strictRequired off:
// "not: {required: [a, b]}" (mutually exclusive fields) is intended.
const ajv = new Ajv2020({ strict: true, strictRequired: false, allErrors: true, allowUnionTypes: true });
addFormats(ajv);

let failures = 0;
let checked = 0;
const fail = (msg) => {
  failures += 1;
  console.error(`  FAIL ${msg}`);
};
const readJson = (p) => JSON.parse(readFileSync(p, "utf8"));

const files = readdirSync(schemasDir).filter((f) => f.endsWith(".schema.json")).sort();
// Register every schema first so cross-file $refs (common.schema.json) resolve.
for (const f of files) {
  const schema = readJson(join(schemasDir, f));
  if (ajv.validateSchema(schema) === false) {
    fail(`${f}: violates the 2020-12 meta-schema: ${ajv.errorsText(ajv.errors)}`);
    continue;
  }
  ajv.addSchema(schema, f);
}

for (const f of files) {
  const name = basename(f, ".schema.json");
  if (name === "common") continue;
  let validate;
  try {
    validate = ajv.getSchema(f);
  } catch (e) {
    fail(`${f}: does not compile (${e.message})`);
    continue;
  }
  if (!validate) continue;
  for (const kind of ["valid", "invalid"]) {
    const dir = join(fixturesDir, name, kind);
    const cases = existsSync(dir) ? readdirSync(dir).filter((c) => c.endsWith(".json")).sort() : [];
    if (cases.length === 0) fail(`${name}: no ${kind} fixtures in protocol/fixtures/${name}/${kind}/`);
    for (const c of cases) {
      checked += 1;
      const ok = validate(readJson(join(dir, c)));
      if (kind === "valid" && !ok) fail(`${name}/valid/${c}: ${ajv.errorsText(validate.errors)}`);
      if (kind === "invalid" && ok) fail(`${name}/invalid/${c}: accepted, but must be rejected`);
    }
  }
}

if (failures > 0) {
  console.error(`protocol: ${failures} failure(s)`);
  process.exit(1);
}
console.log(`protocol: ${files.length} schemas, ${checked} fixtures OK`);
