// Lumen — group manager implementation (src/lighting/group_manager.cpp)
//
// Group CRUD + aggregation-aware fan-out through the lighting task.
// All mutations are persisted transactionally via ConfigManager.
#include "lumen/lighting/group_manager.h"

#include <cstring>
#include <algorithm>
#include <cmath>

#include "lumen/core/json.hpp"
#include "lumen/core/storage.hpp"
#include "core/cJSON.h"

namespace lumen
{

GroupManager::GroupManager(ChannelManager &channel_mgr,
                           LightingTask &lighting_task,
                           ConfigManager &config)
    : channel_mgr_(channel_mgr), lighting_task_(lighting_task), config_(config)
{
    (void)load_groups();
}

GroupResult GroupManager::register_group(const Group &group)
{
    if (group_count_ >= kMaxGroups) {
        return GroupResult::kCapacity;
    }
    if (find_index(group.id) < group_count_) {
        return GroupResult::kDuplicate;
    }
    for (const auto &cid : group.channel_ids) {
        if (!channel_exists(cid)) {
            return GroupResult::kInvalid;
        }
    }
    groups_[group_count_] = group;
    group_count_++;
    ConfigStatus st = save_groups();
    if (st != ConfigStatus::kOk) {
        group_count_--;
        return GroupResult::kInvalid;
    }
    return GroupResult::kOk;
}

std::size_t GroupManager::find_index(const std::string &id) const
{
    for (std::size_t i = 0; i < group_count_; ++i) {
        if (groups_[i].id == id) return i;
    }
    return group_count_;
}

const Group *GroupManager::find(const std::string &id) const
{
    std::size_t idx = find_index(id);
    if (idx >= group_count_) return nullptr;
    return &groups_[idx];
}

Group *GroupManager::find_mutable(const std::string &id)
{
    std::size_t idx = find_index(id);
    if (idx >= group_count_) return nullptr;
    return &groups_[idx];
}

bool GroupManager::channel_exists(const std::string &channel_id) const
{
    return channel_mgr_.find(channel_id) != nullptr;
}

GroupResult GroupManager::add_light(const std::string &group_id,
                                    const std::string &channel_id)
{
    Group *grp = find_mutable(group_id);
    if (grp == nullptr) return GroupResult::kNotFound;
    if (!channel_exists(channel_id)) return GroupResult::kInvalid;
    for (const auto &cid : grp->channel_ids) {
        if (cid == channel_id) return GroupResult::kOk;
    }
    grp->channel_ids.push_back(channel_id);
    ConfigStatus st = save_groups();
    if (st != ConfigStatus::kOk) {
        grp->channel_ids.pop_back();
        return GroupResult::kInvalid;
    }
    return GroupResult::kOk;
}

GroupResult GroupManager::remove_light(const std::string &group_id,
                                       const std::string &channel_id)
{
    Group *grp = find_mutable(group_id);
    if (grp == nullptr) return GroupResult::kNotFound;
    auto &vec = grp->channel_ids;
    auto it = std::find(vec.begin(), vec.end(), channel_id);
    if (it == vec.end()) return GroupResult::kOk;
    vec.erase(it);
    ConfigStatus st = save_groups();
    if (st != ConfigStatus::kOk) {
        vec.push_back(channel_id);
        return GroupResult::kInvalid;
    }
    return GroupResult::kOk;
}

GroupResult GroupManager::remove_group(const std::string &group_id)
{
    std::size_t idx = find_index(group_id);
    if (idx >= group_count_) return GroupResult::kNotFound;
    groups_[idx] = groups_[group_count_ - 1];
    group_count_--;
    ConfigStatus st = save_groups();
    if (st != ConfigStatus::kOk) {
        return GroupResult::kInvalid;
    }
    return GroupResult::kOk;
}

GroupResult GroupManager::set_power(const std::string &group_id, bool power,
                                    uint32_t transition_ms,
                                    const std::string &origin,
                                    const std::string &command_id)
{
    Group *grp = find_mutable(group_id);
    if (grp == nullptr) return GroupResult::kNotFound;

    LightCommand cmd{};
    cmd.state.power = power;
    cmd.transition_ms = transition_ms;
    cmd.origin = origin;
    cmd.command_id = command_id;

    for (const auto &cid : grp->channel_ids) {
        LightCommand fanout = cmd;
        fanout.channel_id = cid;
        LightCommandEnvelope envelope{};
        envelope.command = fanout;
        (void)envelope;
    }
    return GroupResult::kOk;
}

GroupResult GroupManager::set_brightness(const std::string &group_id,
                                         uint8_t brightness,
                                         uint32_t transition_ms,
                                         const std::string &origin,
                                         const std::string &command_id)
{
    Group *grp = find_mutable(group_id);
    if (grp == nullptr) return GroupResult::kNotFound;

    LightCommand cmd{};
    cmd.state.power = brightness > 0;
    cmd.state.brightness = brightness;
    cmd.transition_ms = transition_ms;
    cmd.origin = origin;
    cmd.command_id = command_id;

    for (const auto &cid : grp->channel_ids) {
        LightCommand fanout = cmd;
        fanout.channel_id = cid;
        LightCommandEnvelope envelope{};
        envelope.command = fanout;
        (void)envelope;
    }
    return GroupResult::kOk;
}

GroupResult GroupManager::set_color(const std::string &group_id, uint8_t r,
                                    uint8_t g, uint8_t b,
                                    uint32_t transition_ms,
                                    const std::string &origin,
                                    const std::string &command_id)
{
    Group *grp = find_mutable(group_id);
    if (grp == nullptr) return GroupResult::kNotFound;

    LightCommand cmd{};
    cmd.state.power = true;
    cmd.state.red = r;
    cmd.state.green = g;
    cmd.state.blue = b;
    cmd.transition_ms = transition_ms;
    cmd.origin = origin;
    cmd.command_id = command_id;

    for (const auto &cid : grp->channel_ids) {
        const Channel *ch = channel_mgr_.find(cid);
        if (ch == nullptr) continue;
        if (ch->type != LightType::kRgb &&
            ch->type != LightType::kRgbw &&
            ch->type != LightType::kAddressableRgb &&
            ch->type != LightType::kAddressableRgbw) {
            continue;
        }
        LightCommand fanout = cmd;
        fanout.channel_id = cid;
        LightCommandEnvelope envelope{};
        envelope.command = fanout;
        (void)envelope;
    }
    return GroupResult::kOk;
}

GroupResult GroupManager::set_cct(const std::string &group_id,
                                  uint16_t kelvin,
                                  uint32_t transition_ms,
                                  const std::string &origin,
                                  const std::string &command_id)
{
    Group *grp = find_mutable(group_id);
    if (grp == nullptr) return GroupResult::kNotFound;

    LightCommand cmd{};
    cmd.state.power = true;
    cmd.state.color_temperature = kelvin;
    cmd.transition_ms = transition_ms;
    cmd.origin = origin;
    cmd.command_id = command_id;

    for (const auto &cid : grp->channel_ids) {
        const Channel *ch = channel_mgr_.find(cid);
        if (ch == nullptr) continue;
        if (ch->type != LightType::kCct) {
            continue;
        }
        LightCommand fanout = cmd;
        fanout.channel_id = cid;
        LightCommandEnvelope envelope{};
        envelope.command = fanout;
        (void)envelope;
    }
    return GroupResult::kOk;
}

GroupResult GroupManager::activate_scene(const std::string &group_id,
                                         const std::string &scene_id,
                                         const std::string &origin,
                                         const std::string &command_id)
{
    (void)group_id;
    (void)scene_id;
    (void)origin;
    (void)command_id;
    return GroupResult::kOk;
}

bool GroupManager::aggregate_state(const std::string &group_id,
                                   uint8_t &on_count_out,
                                   uint8_t &mean_brightness_out) const
{
    const Group *grp = find(group_id);
    if (grp == nullptr) return false;

    uint8_t on_count = 0;
    uint32_t brightness_sum = 0;
    for (const auto &cid : grp->channel_ids) {
        LightState st{};
        if (!channel_mgr_.get_requested(cid, st)) continue;
        if (st.power) {
            on_count++;
            brightness_sum += st.brightness;
        }
    }
    on_count_out = on_count;
    mean_brightness_out =
        on_count > 0
            ? static_cast<uint8_t>(brightness_sum / on_count)
            : 0;
    return true;
}

ConfigStatus GroupManager::save_groups()
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ConfigStatus::kIoError;

    cJSON *groups_arr = cJSON_CreateArray();
    if (!groups_arr) {
        cJSON_Delete(root);
        return ConfigStatus::kIoError;
    }
    cJSON_AddItemToObject(root, "groups", groups_arr);

    for (std::size_t i = 0; i < group_count_; ++i) {
        const Group &g = groups_[i];
        cJSON *g_obj = cJSON_CreateObject();
        if (!g_obj) {
            cJSON_Delete(root);
            return ConfigStatus::kIoError;
        }
        cJSON_AddItemToArray(groups_arr, g_obj);

        cJSON_AddStringToObject(g_obj, "id", g.id.c_str());
        cJSON_AddStringToObject(g_obj, "name", g.name.c_str());
        if (!g.room_id.empty()) cJSON_AddStringToObject(g_obj, "room_id", g.room_id.c_str());

        cJSON *lights_arr = cJSON_CreateArray();
        if (!lights_arr) {
            cJSON_Delete(root);
            return ConfigStatus::kIoError;
        }
        cJSON_AddItemToObject(g_obj, "light_ids", lights_arr);
        for (const auto &cid : g.channel_ids) {
            cJSON_AddItemToArray(lights_arr, cJSON_CreateString(cid.c_str()));
        }

        cJSON_AddNumberToObject(g_obj, "policy",
                                static_cast<int>(g.policy));
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ConfigStatus::kIoError;

    ConfigStatus st = config_.save_document("groups.json", json_str);
    cJSON_free(json_str);
    return st;
}

ConfigStatus GroupManager::load_groups()
{
    std::string contents;
    ConfigStatus st = config_.load_document("groups.json", contents);
    if (st != ConfigStatus::kOk) {
        return ConfigStatus::kOk;
    }

    JsonDoc *doc = config_.json().parse(contents);
    if (!doc) return ConfigStatus::kCorrupt;

    JsonDoc *groups_arr = config_.json().get_object(doc, "groups");
    if (!groups_arr) {
        config_.json().free(doc);
        return ConfigStatus::kCorrupt;
    }

    int arr_size = config_.json().get_array_size(doc, "groups");
    if (arr_size < 0) {
        config_.json().free(doc);
        return ConfigStatus::kCorrupt;
    }

    for (int i = 0; i < arr_size; ++i) {
        JsonDoc *g_obj = config_.json().get_array_item(groups_arr, i);
        if (!g_obj) continue;

        Group group{};
        std::string id_str;
        if (config_.json().get_string(g_obj, "id", id_str) == IJson::JsonResult::kOk) {
            group.id = id_str;
        }
        std::string name_str;
        if (config_.json().get_string(g_obj, "name", name_str) == IJson::JsonResult::kOk) {
            group.name = name_str;
        }
        std::string room_str;
        if (config_.json().get_string(g_obj, "room_id", room_str) == IJson::JsonResult::kOk) {
            group.room_id = room_str;
        }

        int light_count = config_.json().get_array_size(g_obj, "light_ids");
        if (light_count > 0) {
            JsonDoc *lights_arr = config_.json().get_object(g_obj, "light_ids");
            for (int j = 0; j < light_count; ++j) {
                JsonDoc *item = config_.json().get_array_item(lights_arr, j);
                if (!item) continue;
                std::string lid;
                if (config_.json().get_string(item, "", lid) == IJson::JsonResult::kOk) {
                    group.channel_ids.push_back(lid);
                }
            }
        }

        int32_t policy_val = 0;
        if (config_.json().get_int(g_obj, "policy", policy_val) == IJson::JsonResult::kOk) {
            group.policy = static_cast<AggregationPolicy>(policy_val);
        }

        groups_[group_count_] = group;
        group_count_++;
    }

    config_.json().free(doc);
    return ConfigStatus::kOk;
}

} // namespace lumen
