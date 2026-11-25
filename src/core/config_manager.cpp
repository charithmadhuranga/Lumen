// Lumen configuration manager — see include/lumen/core/config_manager.hpp
//
// Implements the SPEC §19 load pipeline (read → parse → schema-version check
// → migrate → validate → defaults) and transactional save with config_changed
// notification. Document registry is fixed-size (kMaxDocuments); no heap
// growth.
//
// Storage layer (IStorage) already provides backup + atomic rename, so
// save_document validates first, then delegates the temp-file/rename dance to
// storage.
#include <cstring>

#include "lumen/core/config_manager.hpp"

namespace lumen
{

namespace
{

constexpr char kSchemaVersionKey[] = "schema_version";

} // namespace

ConfigManager::ConfigManager(IStorage &storage, IJson &json, EventBus &bus)
    : storage_(storage), json_(json), bus_(bus)
{
}

bool ConfigManager::register_document(const DocConfig &config)
{
    if (doc_count_ >= kMaxDocuments || config.path == nullptr) {
        return false;
    }
    // No duplicate paths.
    for (std::size_t i = 0; i < doc_count_; ++i) {
        if (std::strcmp(docs_[i].path, config.path) == 0) {
            return false;
        }
    }
    docs_[doc_count_] = config;
    doc_count_++;
    return true;
}

ConfigStatus ConfigManager::find(const char *path, std::size_t &index) const
{
    if (path == nullptr) {
        return ConfigStatus::kNotFound;
    }
    for (std::size_t i = 0; i < doc_count_; ++i) {
        if (std::strcmp(docs_[i].path, path) == 0) {
            index = i;
            return ConfigStatus::kOk;
        }
    }
    return ConfigStatus::kNotFound;
}

ConfigStatus ConfigManager::parse_and_check(const std::string &contents,
                                            uint16_t latest, JsonDoc *&doc,
                                            uint16_t &declared) const
{
    doc = nullptr;
    declared = 0;

    if (contents.empty()) {
        return ConfigStatus::kCorrupt;
    }

    doc = json_.parse(contents);
    if (doc == nullptr) {
        return ConfigStatus::kCorrupt; // unparseable JSON
    }

    int32_t version = 0;
    IJson::JsonResult res = json_.get_int(doc, kSchemaVersionKey, version);
    if (res == IJson::JsonResult::kOk) {
        declared = static_cast<uint16_t>(version);
    }
    // Absent schema_version ⇒ treat as version 0 (will be migrated/stamped).

    if (declared > latest) {
        // Unknown future schema: reject, leave the file intact.
        json_.free(doc);
        doc = nullptr;
        return ConfigStatus::kRejected;
    }
    return ConfigStatus::kOk;
}

ConfigStatus ConfigManager::load_document(const char *path, std::string &out)
{
    out.clear();

    std::size_t index = 0;
    ConfigStatus st = find(path, index);
    if (st != ConfigStatus::kOk) {
        return ConfigStatus::kNotFound;
    }
    const DocConfig &cfg = docs_[index];

    std::string contents;
    StorageError rerr = storage_.read(path, contents);
    if (rerr == StorageError::kNotFound) {
        if (cfg.default_doc != nullptr) {
            out = cfg.default_doc();
        }
        return ConfigStatus::kNotFound;
    }
    if (rerr == StorageError::kCorrupt) {
        if (cfg.default_doc != nullptr) {
            out = cfg.default_doc();
        }
        return ConfigStatus::kCorrupt;
    }
    if (rerr != StorageError::kOk) {
        return ConfigStatus::kIoError;
    }

    JsonDoc *doc = nullptr;
    uint16_t declared = 0;
    st = parse_and_check(contents, cfg.latest_schema_version, doc, declared);
    if (doc != nullptr) {
        json_.free(doc);
    }
    if (st == ConfigStatus::kRejected) {
        return ConfigStatus::kRejected; // file left intact
    }
    if (st != ConfigStatus::kOk) {
        // Unparseable or otherwise bad: fall back to defaults.
        if (cfg.default_doc != nullptr) {
            out = cfg.default_doc();
        }
        return ConfigStatus::kCorrupt;
    }

    // Migrate forward if needed.
    std::string migrated;
    if (declared < cfg.latest_schema_version) {
        if (cfg.migrate == nullptr) {
            // No migration handler for an older version — cannot safely load.
            if (cfg.default_doc != nullptr) {
                out = cfg.default_doc();
            }
            return ConfigStatus::kCorrupt;
        }
        std::string working = contents;
        if (!cfg.migrate(working, declared, cfg.latest_schema_version, json_)) {
            if (cfg.default_doc != nullptr) {
                out = cfg.default_doc();
            }
            return ConfigStatus::kCorrupt;
        }
        // Migrated output must still be valid JSON.
        JsonDoc *check = json_.parse(working);
        if (check == nullptr) {
            if (cfg.default_doc != nullptr) {
                out = cfg.default_doc();
            }
            return ConfigStatus::kCorrupt;
        }
        json_.free(check);
        migrated = working;
    } else {
        migrated = contents;
    }

    // Validate.
    if (cfg.validate != nullptr && !cfg.validate(migrated, json_)) {
        if (cfg.default_doc != nullptr) {
            out = cfg.default_doc();
        }
        return ConfigStatus::kCorrupt;
    }

    out = migrated;

    // Persist the migrated version so we don't re-migrate on every boot.
    if (declared < cfg.latest_schema_version) {
        StorageError werr = storage_.write(path, out, nullptr);
        (void)werr; // best effort; the in-memory config is already consistent
    }

    return ConfigStatus::kOk;
}

ConfigStatus ConfigManager::save_document(const char *path,
                                          const std::string &contents)
{
    std::size_t index = 0;
    ConfigStatus st = find(path, index);
    if (st != ConfigStatus::kOk) {
        return ConfigStatus::kInvalid;
    }
    const DocConfig &cfg = docs_[index];

    JsonDoc *doc = nullptr;
    uint16_t declared = 0;
    st = parse_and_check(contents, cfg.latest_schema_version, doc, declared);
    if (doc != nullptr) {
        json_.free(doc);
    }
    if (st == ConfigStatus::kRejected) {
        return ConfigStatus::kRejected;
    }
    if (st != ConfigStatus::kOk) {
        return ConfigStatus::kInvalid;
    }

    // Migrate forward if the caller supplied an older version.
    std::string final_doc = contents;
    if (declared < cfg.latest_schema_version) {
        if (cfg.migrate == nullptr) {
            return ConfigStatus::kInvalid;
        }
        std::string working = contents;
        if (!cfg.migrate(working, declared, cfg.latest_schema_version, json_)) {
            return ConfigStatus::kInvalid;
        }
        JsonDoc *check = json_.parse(working);
        if (check == nullptr) {
            return ConfigStatus::kInvalid;
        }
        json_.free(check);
        final_doc = working;
    }

    // Stamp the current schema version.
    JsonDoc *stamped = json_.parse(final_doc);
    if (stamped == nullptr) {
        return ConfigStatus::kInvalid;
    }
    json_.set_int(stamped, kSchemaVersionKey,
                  static_cast<int32_t>(cfg.latest_schema_version));
    final_doc = json_.stringify(stamped);
    json_.free(stamped);
    if (final_doc.empty()) {
        return ConfigStatus::kInvalid;
    }

    // Validate before committing.
    if (cfg.validate != nullptr && !cfg.validate(final_doc, json_)) {
        return ConfigStatus::kInvalid;
    }

    // Transactional write (temp → rename, one-generation backup in storage).
    StorageError werr = storage_.write(path, final_doc, nullptr);
    if (werr == StorageError::kInvalid) {
        return ConfigStatus::kInvalid;
    }
    if (werr != StorageError::kOk) {
        return ConfigStatus::kIoError;
    }

    // Publish change notification (consumers re-read what they need).
    bus_.post(EventId::ConfigChanged, EventPriority::Normal);
    return ConfigStatus::kOk;
}

bool migrate_additive(std::string &doc, uint16_t from_version, uint16_t to_version,
                      IJson &json)
{
    (void)doc;
    (void)json;
    return from_version <= to_version;
}

} // namespace lumen
