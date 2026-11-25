// Lumen — automation persistence. See automation_store.h
#include "lumen/automation/automation_store.h"

namespace lumen
{
namespace automation_store
{

namespace
{
std::string path_for(const std::string &id)
{
    return "automations/" + id + ".json";
}
} // namespace

int load_all(IStorage &storage, AutomationEngine &engine, std::string &errors)
{
    int loaded = 0;
    for (const auto &path : storage.list("automations")) {
        if (path.size() < 6 || path.compare(path.size() - 5, 5, ".json") != 0) continue;
        std::string text;
        if (storage.read(path, text) != StorageError::kOk) {
            errors += path + ": unreadable; ";
            continue;
        }
        cJSON *doc = cJSON_Parse(text.c_str());
        Automation a;
        std::string err;
        const bool ok = doc != nullptr && automation_from_json(doc, a, err);
        cJSON_Delete(doc);
        if (!ok) {
            errors += path + ": " + (err.empty() ? "invalid JSON" : err) + "; ";
            continue;
        }
        if (path != path_for(a.id)) {
            errors += path + ": id does not match file name; ";
            continue;
        }
        if (!engine.upsert(a)) {
            errors += path + ": automation limit reached; ";
            break;
        }
        ++loaded;
    }
    return loaded;
}

bool save(IStorage &storage, const Automation &a, const cJSON *graph)
{
    cJSON *doc = automation_to_json(a);
    if (graph != nullptr) {
        cJSON_AddItemToObject(doc, "graph", cJSON_Duplicate(graph, 1));
    }
    char *text = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (text == nullptr) return false;
    const bool ok = storage.write(path_for(a.id), text) == StorageError::kOk;
    cJSON_free(text);
    return ok;
}

bool remove(IStorage &storage, const std::string &id)
{
    const StorageError e = storage.remove(path_for(id));
    return e == StorageError::kOk || e == StorageError::kNotFound;
}

bool read_document(IStorage &storage, const std::string &id, std::string &out)
{
    return storage.read(path_for(id), out) == StorageError::kOk;
}

} // namespace automation_store
} // namespace lumen
