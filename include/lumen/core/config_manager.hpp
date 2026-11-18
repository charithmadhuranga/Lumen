// Lumen configuration manager — include/lumen/core/config_manager.hpp
//
// Document registry per SPEC §19.2. Load pipeline:
//
//   read(storage) -> parse -> schema_version check -> migrate -> validate ->
//   defaults-on-failure
//
// Unknown schema_version (> latest) is REJECTED and the old file is left
// intact (never silently overwritten). Secrets are never stored here (NVS
// only). A successful save publishes `config_changed` on the event bus.
//
// All storage is bounded: fixed document registry, no heap growth.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "lumen/core/event.hpp"
#include "lumen/core/event_bus.hpp"
#include "lumen/core/json.hpp"
#include "lumen/core/storage.hpp"

namespace lumen
{

// Status returned by config operations.
enum class ConfigStatus : uint8_t
{
    kOk = 0,          // document loaded/saved successfully
    kNotFound,        // no document exists; defaults were used
    kCorrupt,         // document unusable; defaults were used
    kInvalid,         // validation failed (caller must not save)
    kRejected,        // unknown schema_version; old file left intact
    kIoError,         // underlying storage failure
};

// Callback types for a registered document. `json` is the (caller-owned)
// adapter used to parse/transform documents during migration/validation.
using DocMigrate = bool (*)(std::string &doc, uint16_t from_version,
                            uint16_t to_version, IJson &json);
using DocValidate = bool (*)(const std::string &doc, IJson &json);
using DocDefault = std::string (*)();

struct DocConfig
{
    const char *path;                 // relative path in storage, e.g. "system.json"
    uint16_t latest_schema_version;   // current schema version for this doc
    DocMigrate migrate = nullptr;     // transforms older versions in-place
    DocValidate validate = nullptr;   // accepts/rejects a document
    DocDefault default_doc = nullptr; // factory for a fresh default document
};

class ConfigManager
{
  public:
    static constexpr std::size_t kMaxDocuments = 16;

    ConfigManager(IStorage &storage, IJson &json, EventBus &bus);

    // Register a document handler. Returns false when the registry is full.
    bool register_document(const DocConfig &config);

    // Load a document into `out`.
    //   kOk        : loaded (migrated if needed), `out` holds the document.
    //   kNotFound  : missing; `out` holds defaults.
    //   kCorrupt   : unusable; `out` holds defaults.
    //   kRejected  : unknown schema_version; file left intact, `out` empty.
    //   kIoError   : storage failure.
    ConfigStatus load_document(const char *path, std::string &out);

    // Save a document. Validates, stamps schema_version, writes
    // transactionally, and publishes config_changed. Returns kInvalid if the
    // document fails validation.
    ConfigStatus save_document(const char *path, const std::string &contents);

  private:
    // Parse + schema check on raw contents. Returns status + parsed doc (or
    // nullptr) + the document's declared schema_version (0 if absent).
    ConfigStatus parse_and_check(const std::string &contents, uint16_t latest,
                                 JsonDoc *&doc, uint16_t &declared) const;

    ConfigStatus find(const char *path, std::size_t &index) const;

    IStorage &storage_;
    IJson &json_;
    EventBus &bus_;
    DocConfig docs_[kMaxDocuments]{};
    std::size_t doc_count_ = 0;
};

} // namespace lumen
