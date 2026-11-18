// Lumen JSON adapter — cJSON implementation of IJson (include/lumen/core/json.hpp).
//
// Vendored cJSON (third_party/cjson/, MIT license) is compiled into both the
// host test build and the firmware build. Config files are small and parsed
// only at boot/write time, so cJSON's heap use here is acceptable (not a hot
// path, AGENTS.md §5).
#include "cJSON.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "lumen/core/json.hpp"

namespace lumen
{

namespace
{

// Convert a cJSON item to an int32_t. Schema versions and small integer
// fields fit in int32; valueint is the saturated int cache.
int32_t to_int32(const cJSON *item)
{
    if (item->valuedouble >= 2147483647.0) {
        return 2147483647;
    }
    if (item->valuedouble <= -2147483648.0) {
        return -2147483648;
    }
    return static_cast<int32_t>(item->valueint);
}

uint32_t to_uint32(const cJSON *item)
{
    if (item->valuedouble < 0.0 || item->valuedouble > 4294967295.0) {
        return 0;
    }
    return static_cast<uint32_t>(item->valueint);
}

// Replace or add a scalar field on an object.
void set_field(cJSON *object, const char *key, cJSON *item)
{
    cJSON *existing = cJSON_DetachItemFromObjectCaseSensitive(object, key);
    if (existing != nullptr) {
        cJSON_Delete(existing);
    }
    cJSON_AddItemToObjectCS(object, key, item);
}

} // namespace

class CjsonAdapter final : public IJson
{
  public:
    JsonDoc *parse(const std::string &json) override
    {
        return reinterpret_cast<JsonDoc *>(cJSON_Parse(json.c_str()));
    }

    void free(JsonDoc *doc) override
    {
        cJSON_Delete(reinterpret_cast<cJSON *>(doc));
    }

    std::string stringify(const JsonDoc *doc) override
    {
        if (doc == nullptr) {
            return {};
        }
        char *printed = cJSON_PrintUnformatted(reinterpret_cast<const cJSON *>(doc));
        std::string out(printed ? printed : "");
        std::free(printed);
        return out;
    }

    bool has(const JsonDoc *doc, const char *key) override
    {
        if (doc == nullptr || key == nullptr) {
            return false;
        }
        return cJSON_HasObjectItem(reinterpret_cast<const cJSON *>(doc), key);
    }

    IJson::JsonResult get_int(const JsonDoc *doc, const char *key, int32_t &out) override
    {
        const cJSON *item = lookup(doc, key);
        if (item == nullptr) {
            return IJson::IJson::JsonResult::kNotFound;
        }
        if (!cJSON_IsNumber(item)) {
            return IJson::IJson::JsonResult::kWrongType;
        }
        out = to_int32(item);
        return IJson::IJson::JsonResult::kOk;
    }

    IJson::JsonResult get_uint(const JsonDoc *doc, const char *key, uint32_t &out) override
    {
        const cJSON *item = lookup(doc, key);
        if (item == nullptr) {
            return IJson::IJson::JsonResult::kNotFound;
        }
        if (!cJSON_IsNumber(item)) {
            return IJson::IJson::JsonResult::kWrongType;
        }
        out = to_uint32(item);
        return IJson::IJson::JsonResult::kOk;
    }

    IJson::JsonResult get_bool(const JsonDoc *doc, const char *key, bool &out) override
    {
        const cJSON *item = lookup(doc, key);
        if (item == nullptr) {
            return IJson::IJson::JsonResult::kNotFound;
        }
        if (cJSON_IsTrue(item)) {
            out = true;
            return IJson::IJson::JsonResult::kOk;
        }
        if (cJSON_IsFalse(item)) {
            out = false;
            return IJson::IJson::JsonResult::kOk;
        }
        return IJson::IJson::JsonResult::kWrongType;
    }

    IJson::JsonResult get_string(const JsonDoc *doc, const char *key,
                          std::string &out) override
    {
        const cJSON *item = lookup(doc, key);
        if (item == nullptr) {
            return IJson::IJson::JsonResult::kNotFound;
        }
        if (!cJSON_IsString(item)) {
            return IJson::IJson::JsonResult::kWrongType;
        }
        const char *value = cJSON_GetStringValue(item);
        out = value ? value : "";
        return IJson::IJson::JsonResult::kOk;
    }

    JsonDoc *get_object(const JsonDoc *doc, const char *key) override
    {
        if (doc == nullptr || key == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<JsonDoc *>(
            cJSON_GetObjectItemCaseSensitive(reinterpret_cast<const cJSON *>(doc), key));
    }

    int32_t get_array_size(const JsonDoc *doc, const char *key) override
    {
        const cJSON *obj = get_object_typed(doc, key);
        if (obj == nullptr || !cJSON_IsArray(obj)) {
            return -1;
        }
        return cJSON_GetArraySize(obj);
    }

    void set_int(JsonDoc *doc, const char *key, int32_t value) override
    {
        cJSON *item = cJSON_CreateNumber(static_cast<double>(value));
        if (item != nullptr && doc != nullptr) {
            set_field(reinterpret_cast<cJSON *>(doc), key, item);
        } else if (item != nullptr) {
            cJSON_Delete(item);
        }
    }

    void set_uint(JsonDoc *doc, const char *key, uint32_t value) override
    {
        cJSON *item = cJSON_CreateNumber(static_cast<double>(value));
        if (item != nullptr && doc != nullptr) {
            set_field(reinterpret_cast<cJSON *>(doc), key, item);
        } else if (item != nullptr) {
            cJSON_Delete(item);
        }
    }

    void set_bool(JsonDoc *doc, const char *key, bool value) override
    {
        cJSON *item = cJSON_CreateBool(value ? 1 : 0);
        if (item != nullptr && doc != nullptr) {
            set_field(reinterpret_cast<cJSON *>(doc), key, item);
        } else if (item != nullptr) {
            cJSON_Delete(item);
        }
    }

    void set_string(JsonDoc *doc, const char *key, const char *value) override
    {
        cJSON *item = cJSON_CreateString(value);
        if (item != nullptr && doc != nullptr) {
            set_field(reinterpret_cast<cJSON *>(doc), key, item);
        } else if (item != nullptr) {
            cJSON_Delete(item);
        }
    }

    void add_object(JsonDoc *doc, const char *key) override
    {
        if (doc == nullptr) {
            return;
        }
        cJSON *item = cJSON_CreateObject();
        if (item != nullptr) {
            cJSON_AddItemToObjectCS(reinterpret_cast<cJSON *>(doc), key, item);
        }
    }

    void add_array(JsonDoc *doc, const char *key) override
    {
        if (doc == nullptr) {
            return;
        }
        cJSON *item = cJSON_CreateArray();
        if (item != nullptr) {
            cJSON_AddItemToObjectCS(reinterpret_cast<cJSON *>(doc), key, item);
        }
    }

  private:
    const cJSON *lookup(const JsonDoc *doc, const char *key) const
    {
        if (doc == nullptr || key == nullptr) {
            return nullptr;
        }
        return cJSON_GetObjectItemCaseSensitive(reinterpret_cast<const cJSON *>(doc), key);
    }

    const cJSON *get_object_typed(const JsonDoc *doc, const char *key) const
    {
        return lookup(doc, key);
    }
};

IJson *create_cjson_adapter()
{
    return new CjsonAdapter();
}

} // namespace lumen
