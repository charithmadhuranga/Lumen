// Lumen — board profile loader implementation. See board_profile_loader.h
#include "lumen/hardware/board_profile_loader.h"

#include "core/cJSON.h"

#include <cstdio>
#include <cstring>

namespace lumen
{

namespace
{

// stdio instead of <fstream>: iostreams pull the whole locale machinery
// (~100 KB) into the firmware image.
bool read_file(const std::string &path, std::string &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(&out[0], 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

bool get_string(cJSON *object, const char *key, std::string &out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item || !cJSON_IsString(item)) {
        return false;
    }
    const char *v = cJSON_GetStringValue(item);
    out = v ? v : "";
    return true;
}

bool get_int(cJSON *object, const char *key, int32_t &out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item || !cJSON_IsNumber(item)) {
        return false;
    }
    out = static_cast<int32_t>(item->valueint);
    return true;
}

bool get_bool(cJSON *object, const char *key, bool &out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) {
        return false;
    }
    out = cJSON_IsTrue(item);
    return true;
}

} // namespace

bool load_board_profile(const std::string &json, BoardProfile &out)
{
    if (json.empty()) {
        return false;
    }

    cJSON *root = cJSON_Parse(json.c_str());
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    bool ok = true;

    int32_t sv = 0;
    if (get_int(root, "schema_version", sv)) {
        out.schema_version = sv;
    } else {
        ok = false; // required
    }

    if (!get_string(root, "id", out.id)) ok = false;
    if (!get_string(root, "name", out.name)) ok = false;
    if (!get_string(root, "mcu", out.mcu)) ok = false;

    get_string(root, "manufacturer", out.manufacturer);
    get_string(root, "description", out.description);

    // Resources.
    cJSON *resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
    if (resources && cJSON_IsArray(resources)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, resources)
        {
            if (!cJSON_IsObject(item)) {
                continue;
            }
            BoardResource r;
            get_string(item, "id", r.id);
            get_string(item, "type", r.type);
            int32_t g = -1;
            if (get_int(item, "gpio", g)) r.gpio = g;
            get_string(item, "direction", r.direction);
            get_bool(item, "inverted", r.inverted);
            get_string(item, "pull", r.pull);
            bool ss = false;
            if (get_bool(item, "safe_state", ss)) {
                r.safe_state_set = true;
                r.safe_state = ss;
            }
            get_int(item, "port", r.port);
            get_int(item, "frequency", r.frequency);
            cJSON *pins = cJSON_GetObjectItemCaseSensitive(item, "pins");
            if (pins && cJSON_IsObject(pins)) {
                cJSON *pin = nullptr;
                cJSON_ArrayForEach(pin, pins)
                {
                    if (pin->string && cJSON_IsNumber(pin)) {
                        r.pins.emplace_back(pin->string,
                                            static_cast<int32_t>(pin->valueint));
                    }
                }
            }
            out.resources.push_back(r);
        }
    }

    // Peripherals.
    cJSON *peripherals = cJSON_GetObjectItemCaseSensitive(root, "peripherals");
    if (peripherals && cJSON_IsArray(peripherals)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, peripherals)
        {
            if (!cJSON_IsObject(item)) {
                continue;
            }
            BoardPeripheral p;
            get_string(item, "id", p.id);
            get_string(item, "type", p.type);
            get_string(item, "bus", p.bus);
            int32_t addr = -1;
            if (get_int(item, "address", addr)) p.address = addr;
            int32_t ch = 0;
            if (get_int(item, "channels", ch)) p.channels = ch;
            int32_t freq = 0;
            if (get_int(item, "frequency", freq)) p.frequency = freq;
            get_bool(item, "unverified", p.unverified);
            get_bool(item, "address_verification_required",
                     p.address_verification_required);
            get_string(item, "unverified_note", p.unverified_note);
            get_bool(item, "active_low", p.active_low);
            cJSON *settings = cJSON_GetObjectItemCaseSensitive(item, "settings");
            if (settings && cJSON_IsObject(settings)) {
                char *raw = cJSON_PrintUnformatted(settings);
                if (raw) {
                    p.settings_json = raw;
                    cJSON_free(raw);
                }
            }
            out.peripherals.push_back(p);
        }
    }

    get_bool(root, "unverified", out.unverified);

    cJSON_Delete(root);
    return ok;
}

bool load_board_profile_file(const std::string &path, BoardProfile &out)
{
    std::string json;
    if (!read_file(path, json)) {
        return false;
    }
    return load_board_profile(json, out);
}

} // namespace lumen
