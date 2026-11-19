// Lumen JSON abstraction — include/lumen/core/json.hpp
//
// A thin, platform-free interface over a JSON parser so the config manager
// (and later the web API) never depend on cJSON directly. cJSON is vendored
// in third_party/cjson/ and adapted here via src/core/json_adapter.cpp.
//
// All handles are opaque (JsonDoc). The caller owns the lifetime: every
// parse() must be matched by free().
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lumen
{

struct JsonDoc; // opaque, owned by the caller until free()

// Result codes for get_* accessors.
class IJson
{
  public:
    // Result codes for get_* accessors.
    enum class JsonResult : uint8_t
    {
        kOk = 0,
        kNotFound,   // key not present
        kWrongType,  // present but not the requested type
        kInvalid,    // null doc or other error
    };

    virtual ~IJson() = default;

    // Parse a JSON document. Returns nullptr on parse failure.
    virtual JsonDoc *parse(const std::string &json) = 0;

    // Free a previously parsed document.
    virtual void free(JsonDoc *doc) = 0;

    // Stringify a document back to compact JSON.
    virtual std::string stringify(const JsonDoc *doc) = 0;

    // Object accessors.
    virtual bool has(const JsonDoc *doc, const char *key) = 0;
    virtual JsonResult get_int(const JsonDoc *doc, const char *key,
                               int32_t &out) = 0;
    virtual JsonResult get_uint(const JsonDoc *doc, const char *key,
                                uint32_t &out) = 0;
    virtual JsonResult get_bool(const JsonDoc *doc, const char *key,
                                bool &out) = 0;
    virtual JsonResult get_string(const JsonDoc *doc, const char *key,
                                  std::string &out) = 0;

    // Nested accessors.
    virtual JsonDoc *get_object(const JsonDoc *doc, const char *key) = 0;
    virtual int32_t get_array_size(const JsonDoc *doc, const char *key) = 0;
    virtual JsonDoc *get_array_item(const JsonDoc *array, int index) = 0;

    // Mutators (used by migrations).
    virtual void set_int(JsonDoc *doc, const char *key, int32_t value) = 0;
    virtual void set_uint(JsonDoc *doc, const char *key, uint32_t value) = 0;
    virtual void set_bool(JsonDoc *doc, const char *key, bool value) = 0;
    virtual void set_string(JsonDoc *doc, const char *key,
                            const char *value) = 0;

    // Add a key to an object (creates object if doc is null-root object).
    virtual void add_object(JsonDoc *doc, const char *key) = 0;
    virtual void add_array(JsonDoc *doc, const char *key) = 0;
};

// Factory: cJSON-backed implementation (src/core/json_adapter.cpp).
IJson *create_cjson_adapter();

} // namespace lumen
