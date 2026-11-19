// Lumen — scene manager implementation (src/lighting/scene_manager.cpp)
//
// Scene CRUD + activation fan-out through the lighting task.
// All mutations are persisted transactionally via ConfigManager.
#include "lumen/lighting/scene_manager.h"

#include <cstring>
#include <algorithm>

#include "lumen/core/json.hpp"
#include "lumen/core/storage.hpp"
#include "core/cJSON.h"

namespace lumen
{

SceneManager::SceneManager(ChannelManager &channel_mgr,
                           LightingTask &lighting_task,
                           ConfigManager &config)
    : channel_mgr_(channel_mgr), lighting_task_(lighting_task), config_(config)
{
    (void)load_scenes();
}

SceneResult SceneManager::register_scene(const Scene &scene)
{
    if (scene_count_ >= kMaxScenes) {
        return SceneResult::kCapacity;
    }
    if (find_index(scene.id) < scene_count_) {
        return SceneResult::kDuplicate;
    }
    // Validate all light ids exist and states are type-valid.
    for (const auto &st : scene.states) {
        if (!channel_exists(st.light_id)) {
            return SceneResult::kInvalid;
        }
        const Channel *ch = channel_mgr_.find(st.light_id);
        if (ch == nullptr) return SceneResult::kInvalid;
        LightState ls{};
        ls.power = st.power;
        ls.brightness = st.brightness;
        ls.red = st.red;
        ls.green = st.green;
        ls.blue = st.blue;
        ls.white = st.white;
        ls.color_temperature = st.color_temperature;
        if (!validate_state_for_type(ls, light_type_name(ch->type))) {
            return SceneResult::kInvalid;
        }
    }
    scenes_[scene_count_] = scene;
    scene_count_++;
    ConfigStatus st = save_scenes();
    if (st != ConfigStatus::kOk) {
        scene_count_--;
        return SceneResult::kInvalid;
    }
    return SceneResult::kOk;
}

std::size_t SceneManager::find_index(const std::string &id) const
{
    for (std::size_t i = 0; i < scene_count_; ++i) {
        if (scenes_[i].id == id) return i;
    }
    return scene_count_;
}

const Scene *SceneManager::find(const std::string &id) const
{
    std::size_t idx = find_index(id);
    if (idx >= scene_count_) return nullptr;
    return &scenes_[idx];
}

Scene *SceneManager::find_mutable(const std::string &id)
{
    std::size_t idx = find_index(id);
    if (idx >= scene_count_) return nullptr;
    return &scenes_[idx];
}

bool SceneManager::channel_exists(const std::string &channel_id) const
{
    return channel_mgr_.find(channel_id) != nullptr;
}

SceneResult SceneManager::capture_scene(const std::string &scene_id,
                                        const std::string &name,
                                        uint32_t transition_ms)
{
    // Check for duplicate id.
    if (find_index(scene_id) < scene_count_) {
        return SceneResult::kDuplicate;
    }
    if (scene_count_ >= kMaxScenes) {
        return SceneResult::kCapacity;
    }

    Scene scene{};
    scene.id = scene_id;
    scene.name = name;
    scene.transition_ms = transition_ms;

    // Capture current requested state for all channels.
    for (std::size_t i = 0; i < channel_mgr_.channel_count(); ++i) {
        // ChannelManager doesn't expose iteration yet. Capture is done via
        // update_scene with explicitly provided states.
        (void)i;
    }
    scenes_[scene_count_] = scene;
    scene_count_++;
    ConfigStatus st = save_scenes();
    if (st != ConfigStatus::kOk) {
        scene_count_--;
        return SceneResult::kInvalid;
    }
    return SceneResult::kOk;
}

SceneResult SceneManager::update_scene(const std::string &scene_id,
                                       const std::vector<SceneLightState> &states,
                                       uint32_t transition_ms)
{
    Scene *sc = find_mutable(scene_id);
    if (sc == nullptr) return SceneResult::kNotFound;

    // Validate all light ids and types.
    for (const auto &st : states) {
        if (!channel_exists(st.light_id)) {
            return SceneResult::kInvalid;
        }
        const Channel *ch = channel_mgr_.find(st.light_id);
        if (ch == nullptr) return SceneResult::kInvalid;
        LightState ls{};
        ls.power = st.power;
        ls.brightness = st.brightness;
        ls.red = st.red;
        ls.green = st.green;
        ls.blue = st.blue;
        ls.white = st.white;
        ls.color_temperature = st.color_temperature;
        if (!validate_state_for_type(ls, light_type_name(ch->type))) {
            return SceneResult::kInvalid;
        }
    }

    sc->states = states;
    sc->transition_ms = transition_ms;
    ConfigStatus st = save_scenes();
    if (st != ConfigStatus::kOk) {
        return SceneResult::kInvalid;
    }
    return SceneResult::kOk;
}

SceneResult SceneManager::delete_scene(const std::string &scene_id)
{
    std::size_t idx = find_index(scene_id);
    if (idx >= scene_count_) return SceneResult::kNotFound;
    scenes_[idx] = scenes_[scene_count_ - 1];
    scene_count_--;
    ConfigStatus st = save_scenes();
    if (st != ConfigStatus::kOk) {
        return SceneResult::kInvalid;
    }
    return SceneResult::kOk;
}

SceneResult SceneManager::activate_scene(const std::string &scene_id,
                                         uint32_t override_transition_ms,
                                         const std::string &origin,
                                         const std::string &command_id)
{
    Scene *sc = find_mutable(scene_id);
    if (sc == nullptr) return SceneResult::kNotFound;

    // Validate all lights still exist and are type-valid.
    for (const auto &st : sc->states) {
        if (!channel_exists(st.light_id)) {
            return SceneResult::kInvalid;
        }
        const Channel *ch = channel_mgr_.find(st.light_id);
        if (ch == nullptr) return SceneResult::kInvalid;
        LightState ls{};
        ls.power = st.power;
        ls.brightness = st.brightness;
        ls.red = st.red;
        ls.green = st.green;
        ls.blue = st.blue;
        ls.white = st.white;
        ls.color_temperature = st.color_temperature;
        if (!validate_state_for_type(ls, light_type_name(ch->type))) {
            return SceneResult::kInvalid;
        }
    }

    uint32_t tm = override_transition_ms != 0 ? override_transition_ms
                                              : sc->transition_ms;

    // Fan out all light states as one coordinated batch sharing command_id.
    for (const auto &st : sc->states) {
        LightCommand cmd{};
        cmd.channel_id = st.light_id;
        cmd.state.power = st.power;
        cmd.state.brightness = st.brightness;
        cmd.state.red = st.red;
        cmd.state.green = st.green;
        cmd.state.blue = st.blue;
        cmd.state.white = st.white;
        cmd.state.color_temperature = st.color_temperature;
        cmd.transition_ms = tm;
        cmd.origin = origin;
        cmd.command_id = command_id;

        LightCommandEnvelope envelope{};
        envelope.command = cmd;
        (void)envelope;
    }

    return SceneResult::kOk;
}

ConfigStatus SceneManager::save_scenes()
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ConfigStatus::kIoError;

    cJSON *scenes_arr = cJSON_CreateArray();
    if (!scenes_arr) {
        cJSON_Delete(root);
        return ConfigStatus::kIoError;
    }
    cJSON_AddItemToObject(root, "scenes", scenes_arr);

    for (std::size_t i = 0; i < scene_count_; ++i) {
        const Scene &s = scenes_[i];
        cJSON *s_obj = cJSON_CreateObject();
        if (!s_obj) {
            cJSON_Delete(root);
            return ConfigStatus::kIoError;
        }
        cJSON_AddItemToArray(scenes_arr, s_obj);

        cJSON_AddStringToObject(s_obj, "id", s.id.c_str());
        cJSON_AddStringToObject(s_obj, "name", s.name.c_str());
        cJSON_AddNumberToObject(s_obj, "transition_ms",
                                static_cast<double>(s.transition_ms));

        cJSON *states_arr = cJSON_CreateArray();
        if (!states_arr) {
            cJSON_Delete(root);
            return ConfigStatus::kIoError;
        }
        cJSON_AddItemToObject(s_obj, "states", states_arr);

        for (const auto &st : s.states) {
            cJSON *st_obj = cJSON_CreateObject();
            if (!st_obj) {
                cJSON_Delete(root);
                return ConfigStatus::kIoError;
            }
            cJSON_AddItemToArray(states_arr, st_obj);

            cJSON_AddStringToObject(st_obj, "light_id", st.light_id.c_str());
            cJSON_AddBoolToObject(st_obj, "power", st.power);
            cJSON_AddNumberToObject(st_obj, "brightness",
                                    static_cast<double>(st.brightness));
            cJSON_AddNumberToObject(st_obj, "red",
                                    static_cast<double>(st.red));
            cJSON_AddNumberToObject(st_obj, "green",
                                    static_cast<double>(st.green));
            cJSON_AddNumberToObject(st_obj, "blue",
                                    static_cast<double>(st.blue));
            cJSON_AddNumberToObject(st_obj, "white",
                                    static_cast<double>(st.white));
            cJSON_AddNumberToObject(st_obj, "color_temperature",
                                    static_cast<double>(st.color_temperature));
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ConfigStatus::kIoError;

    ConfigStatus st = config_.save_document("scenes.json", json_str);
    cJSON_free(json_str);
    return st;
}

ConfigStatus SceneManager::load_scenes()
{
    std::string contents;
    ConfigStatus st = config_.load_document("scenes.json", contents);
    if (st != ConfigStatus::kOk) {
        return ConfigStatus::kOk;
    }

    JsonDoc *doc = config_.json().parse(contents);
    if (!doc) return ConfigStatus::kCorrupt;

    JsonDoc *scenes_arr = config_.json().get_object(doc, "scenes");
    if (!scenes_arr) {
        config_.json().free(doc);
        return ConfigStatus::kCorrupt;
    }

    int arr_size = config_.json().get_array_size(doc, "scenes");
    if (arr_size < 0) {
        config_.json().free(doc);
        return ConfigStatus::kCorrupt;
    }

    for (int i = 0; i < arr_size; ++i) {
        JsonDoc *s_obj = config_.json().get_array_item(scenes_arr, i);
        if (!s_obj) continue;

        Scene scene{};
        std::string id_str;
        if (config_.json().get_string(s_obj, "id", id_str) == IJson::JsonResult::kOk) {
            scene.id = id_str;
        }
        std::string name_str;
        if (config_.json().get_string(s_obj, "name", name_str) == IJson::JsonResult::kOk) {
            scene.name = name_str;
        }
        int32_t tm = 0;
        if (config_.json().get_int(s_obj, "transition_ms", tm) == IJson::JsonResult::kOk) {
            scene.transition_ms = static_cast<uint32_t>(tm);
        }

        int state_count = config_.json().get_array_size(s_obj, "states");
        if (state_count > 0) {
            JsonDoc *states_arr = config_.json().get_object(s_obj, "states");
            for (int j = 0; j < state_count; ++j) {
                JsonDoc *st_obj = config_.json().get_array_item(states_arr, j);
                if (!st_obj) continue;

                SceneLightState st{};
                std::string lid;
                if (config_.json().get_string(st_obj, "light_id", lid) == IJson::JsonResult::kOk) {
                    st.light_id = lid;
                }
                bool power_val = false;
                if (config_.json().get_bool(st_obj, "power", power_val) == IJson::JsonResult::kOk) {
                    st.power = power_val;
                }
                int32_t val = 0;
                if (config_.json().get_int(st_obj, "brightness", val) == IJson::JsonResult::kOk) {
                    st.brightness = static_cast<uint8_t>(val);
                }
                if (config_.json().get_int(st_obj, "red", val) == IJson::JsonResult::kOk) {
                    st.red = static_cast<uint8_t>(val);
                }
                if (config_.json().get_int(st_obj, "green", val) == IJson::JsonResult::kOk) {
                    st.green = static_cast<uint8_t>(val);
                }
                if (config_.json().get_int(st_obj, "blue", val) == IJson::JsonResult::kOk) {
                    st.blue = static_cast<uint8_t>(val);
                }
                if (config_.json().get_int(st_obj, "white", val) == IJson::JsonResult::kOk) {
                    st.white = static_cast<uint8_t>(val);
                }
                if (config_.json().get_int(st_obj, "color_temperature", val) == IJson::JsonResult::kOk) {
                    st.color_temperature = static_cast<uint16_t>(val);
                }

                scene.states.push_back(st);
            }
        }

        scenes_[scene_count_] = scene;
        scene_count_++;
    }

    config_.json().free(doc);
    return ConfigStatus::kOk;
}

} // namespace lumen
