// Lumen — configuration backup and transactional restore (SPEC §17.2, §19.4).
//
// A backup is one JSON document:
//   {"format":"lumen-backup","version":1,"firmware":"0.1.0","board":"...",
//    "created":<unix s or 0>,"documents":{"lights.json":{...},
//    "automations/porch.json":{...}, ...}}
// It contains the JSON configuration only. Secrets (passwords, MQTT password,
// integration token, Wi-Fi keys) live in NVS and are never part of it.
//
// Both directions stream: a backup is produced document by document and a
// restore is parsed incrementally, so RAM use is bounded by one document
// (kMaxDocument), not by the size of the backup.
//
// Restore is transactional: the result is assembled in "<root>.restore",
// then swapped in with two directory renames ("<root>" -> "<root>.old",
// staging -> "<root>"). Any failure before the swap leaves the current
// configuration untouched; recover_interrupted() at boot repairs a swap that
// was cut by power loss. The controller restarts afterwards to load it.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace lumen::backup
{

constexpr int kFormatVersion = 1;
constexpr std::size_t kMaxDocuments = 200;
constexpr std::size_t kMaxDocument = 32 * 1024;
constexpr std::size_t kMaxBytes = 1024 * 1024;

// Output sink: returns false to abort.
using Sink = std::function<bool(const char *data, std::size_t len)>;
// Input source: bytes read (> 0), 0 at end, < 0 on error.
using Source = std::function<int(char *buf, std::size_t len)>;

// `sections` filters by top-level name without ".json" ("lights", "scenes",
// "automations" = the automations/ folder); empty = everything.
bool write(const std::string &root, const std::string &firmware, const std::string &board, long long created,
           const std::vector<std::string> &sections, const Sink &sink);
std::string create(const std::string &root, const std::string &firmware, const std::string &board, long long created,
                   const std::vector<std::string> &sections);

// replace_all = true: the configuration becomes exactly the backup.
// false: only the documents in the backup are replaced, others are kept.
bool restore_stream(const std::string &root, const Source &source, bool replace_all, std::string &error,
                    std::vector<std::string> *restored = nullptr);
bool restore(const std::string &root, const std::string &json, bool replace_all, std::string &error,
             std::vector<std::string> *restored = nullptr);

// Boot-time repair of an interrupted swap. Returns true if it changed anything.
bool recover_interrupted(const std::string &root);

// Exposed for tests: relative document path rules.
bool valid_document_path(const std::string &path);

} // namespace lumen::backup
