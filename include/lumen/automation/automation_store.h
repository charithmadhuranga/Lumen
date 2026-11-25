// Lumen — automation persistence (SPEC §19.2: /config/automations/<id>.json).
//
// Each automation is its own document so the device never parses one huge
// JSON blob. The canonical fields are re-serialised on save; the advanced
// editor's opaque "graph" (node positions) is stored alongside them.
#pragma once

#include <string>

#include "lumen/automation/automation_engine.h"
#include "lumen/core/storage.hpp"

namespace lumen
{
namespace automation_store
{

// Loads every automations/*.json into the engine. Invalid files are skipped
// and reported in `errors` (never deleted). Returns the number loaded.
int load_all(IStorage &storage, AutomationEngine &engine, std::string &errors);

// Writes the canonical document (+ graph if given). Transactional.
bool save(IStorage &storage, const Automation &a, const cJSON *graph);

bool remove(IStorage &storage, const std::string &id);

// Full stored document (canonical + graph) for GET /automations/{id}.
bool read_document(IStorage &storage, const std::string &id, std::string &out);

} // namespace automation_store
} // namespace lumen
