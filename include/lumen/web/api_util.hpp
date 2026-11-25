// Lumen — small helpers shared by the API route modules.
#pragma once

#include <string>
#include <vector>

#include "core/cJSON.h"

namespace lumen
{
namespace api
{

inline const cJSON *field(const cJSON *obj, const char *key)
{
    return obj != nullptr ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
}

inline bool has(const cJSON *obj, const char *key)
{
    return field(obj, key) != nullptr;
}

inline bool get_string(const cJSON *obj, const char *key, std::string &out, std::size_t max_len = 128)
{
    const cJSON *it = field(obj, key);
    if (!cJSON_IsString(it) || it->valuestring == nullptr) return false;
    out = it->valuestring;
    if (out.size() > max_len) out.resize(max_len);
    return true;
}

inline std::string str_or(const cJSON *obj, const char *key, const std::string &fallback)
{
    std::string v;
    return get_string(obj, key, v) ? v : fallback;
}

inline bool get_number(const cJSON *obj, const char *key, double &out)
{
    const cJSON *it = field(obj, key);
    if (!cJSON_IsNumber(it)) return false;
    out = it->valuedouble;
    return true;
}

inline bool get_bool(const cJSON *obj, const char *key, bool &out)
{
    const cJSON *it = field(obj, key);
    if (!cJSON_IsBool(it)) return false;
    out = cJSON_IsTrue(it);
    return true;
}

inline bool get_string_array(const cJSON *obj, const char *key, std::vector<std::string> &out,
                             std::size_t max_items = 64)
{
    const cJSON *arr = field(obj, key);
    if (!cJSON_IsArray(arr)) return false;
    out.clear();
    const cJSON *it = nullptr;
    cJSON_ArrayForEach(it, arr)
    {
        if (!cJSON_IsString(it) || it->valuestring == nullptr) return false;
        if (out.size() >= max_items) return false;
        out.emplace_back(it->valuestring);
    }
    return true;
}

inline cJSON *string_array(const std::vector<std::string> &items)
{
    cJSON *arr = cJSON_CreateArray();
    for (const auto &s : items) cJSON_AddItemToArray(arr, cJSON_CreateString(s.c_str()));
    return arr;
}

inline std::string to_string(cJSON *obj)
{
    char *text = cJSON_PrintUnformatted(obj);
    std::string out = text != nullptr ? text : "";
    cJSON_free(text);
    return out;
}

} // namespace api
} // namespace lumen
