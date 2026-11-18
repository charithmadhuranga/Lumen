#!/usr/bin/env node
// Lumen schema validation (Phase 0 plan §0.3).
//
// 1. Every schemas/*.schema.json must compile against the JSON Schema
//    draft 2020-12 meta-schema.
// 2. Every test/fixtures/<schema>.valid.json must validate.
// 3. Every test/fixtures/<schema>.invalid.json must FAIL validation.
//
// Exit code 0 = all green. Any violation exits 1 with a per-file report.

import { readFileSync, readdirSync } from "node:fs";
import { basename, join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import Ajv2020 from "ajv/dist/2020.js";
import addFormats from "ajv-formats";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const schemasDir = join(root, "schemas");
const fixturesDir = join(root, "test", "fixtures");

// allowUnionTypes: the board-profile schema (SPEC 6.2) is kept verbatim and
// uses a union type for safe_state — the schema itself stays authoritative.
const ajv = new Ajv2020({ strict: true, allErrors: true, allowUnionTypes: true });
addFormats(ajv);

let failures = 0;
const fail = (msg) => {
  failures += 1;
  console.error(`  FAIL ${msg}`);
};

// --- 1. Schemas against the 2020-12 meta-schema -----------------------
const schemaFiles = readdirSync(schemasDir)
  .filter((f) => f.endsWith(".schema.json"))
  .sort();

if (schemaFiles.length === 0) {
  fail("no schema files found in schemas/");
}

const validators = new Map(); // name -> compiled validate fn

for (const file of schemaFiles) {
  const name = basename(file, ".schema.json");
  const raw = readFileSync(join(schemasDir, file), "utf8");
  let schema;
  try {
    schema = JSON.parse(raw);
  } catch (e) {
    fail(`${file}: invalid JSON (${e.message})`);
    continue;
  }

  if (ajv.validateSchema(schema) === false) {
    fail(`${file}: violates the 2020-12 meta-schema`);
    for (const err of ajv.errors ?? []) {
      console.error(`       ${err.instancePath} ${err.message}`);
    }
    continue;
  }

  try {
    validators.set(name, ajv.compile(schema));
  } catch (e) {
    fail(`${file}: compile error (${e.message})`);
    continue;
  }
  console.log(`  ok   ${file}`);
}

// --- 2/3. Fixtures ----------------------------------------------------
const fixtureFiles = readdirSync(fixturesDir)
  .filter((f) => f.endsWith(".json"))
  .sort();

const seen = new Set();
for (const file of fixtureFiles) {
  const m = /^(.+)\.(valid|invalid)\.json$/.exec(file);
  if (!m) {
    fail(`fixture ${file}: name must be <schema>.valid.json or <schema>.invalid.json`);
    continue;
  }
  const [, name, expectation] = m;
  const validate = validators.get(name);
  if (!validate) {
    fail(`fixture ${file}: no schemas/${name}.schema.json to validate against`);
    continue;
  }
  seen.add(file);

  let doc;
  try {
    doc = JSON.parse(readFileSync(join(fixturesDir, file), "utf8"));
  } catch (e) {
    fail(`${file}: invalid JSON (${e.message})`);
    continue;
  }

  const ok = validate(doc);
  if (expectation === "valid") {
    if (ok) {
      console.log(`  ok   ${file}`);
    } else {
      fail(`${file}: expected VALID but schema rejected it`);
      for (const err of validate.errors ?? []) {
        console.error(`       ${err.instancePath} ${err.message}`);
      }
    }
  } else {
    if (!ok) {
      console.log(`  ok   ${file} (correctly rejected)`);
    } else {
      fail(`${file}: expected INVALID but schema accepted it`);
    }
  }
}

// Every schema must have at least one valid fixture.
for (const name of validators.keys()) {
  if (!seen.has(`${name}.valid.json`)) {
    fail(`schemas/${name}.schema.json has no test/fixtures/${name}.valid.json`);
  }
}

// --- 4. Board profiles (boards/*.json) against board-profile schema ----
// Lands as real profiles arrive (Phase 2/3); skipped while boards/ holds
// only README.
const boardValidate = validators.get("board-profile");
const boardsDir = join(root, "boards");
const boardFiles = readdirSync(boardsDir).filter((f) => f.endsWith(".json")).sort();
if (boardValidate && boardFiles.length > 0) {
  for (const file of boardFiles) {
    let doc;
    try {
      doc = JSON.parse(readFileSync(join(boardsDir, file), "utf8"));
    } catch (e) {
      fail(`boards/${file}: invalid JSON (${e.message})`);
      continue;
    }
    if (boardValidate(doc)) {
      console.log(`  ok   boards/${file}`);
    } else {
      fail(`boards/${file}: rejected by board-profile schema`);
      for (const err of boardValidate.errors ?? []) {
        console.error(`       ${err.instancePath} ${err.message}`);
      }
    }
  }
}

console.log(
  `\n${schemaFiles.length} schemas, ${fixtureFiles.length} fixtures, ${boardFiles.length} boards, ${failures} failure(s)`,
);
process.exit(failures === 0 ? 0 : 1);
