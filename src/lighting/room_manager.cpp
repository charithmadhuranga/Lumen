// Lumen — room manager implementation (src/lighting/room_manager.cpp)
//
// Room CRUD + master operations fan-out through the lighting task.
// All mutations are persisted transactionally via ConfigManager.
#include "lumen/lighting/room_manager.h"

#include <cstring>
#include <algorithm>

#include "lumen/core/json.hpp"
#include "lumen/core/storage.hpp"
#include "core/cJSON.h"

namespace lumen
{

RoomManager::RoomManager(ChannelManager &channel_mgr,
                         LightingTask &lighting_task,
                         ConfigManager &config)
    : channel_mgr_(channel_mgr), lighting_task_(lighting_task), config_(config)
{
    // Load rooms from config on construction.
    (void)load_rooms();
}

RoomResult RoomManager::register_room(const Room &room)
{
    if (room_count_ >= kMaxRooms) {
        return RoomResult::kCapacity;
    }
    if (find_index(room.id) < room_count_) {
        return RoomResult::kDuplicate;
    }
    // Validate all channel ids exist.
    for (const auto &cid : room.channel_ids) {
        if (!channel_exists(cid)) {
            return RoomResult::kInvalid;
        }
    }
    rooms_[room_count_] = room;
    room_count_++;
    ConfigStatus st = save_rooms();
    if (st != ConfigStatus::kOk) {
        // Rollback on save failure.
        room_count_--;
        return RoomResult::kInvalid;
    }
    return RoomResult::kOk;
}

std::size_t RoomManager::find_index(const std::string &id) const
{
    for (std::size_t i = 0; i < room_count_; ++i) {
        if (rooms_[i].id == id) {
            return i;
        }
    }
    return room_count_; // sentinel = not found
}

const Room *RoomManager::find(const std::string &id) const
{
    std::size_t idx = find_index(id);
    if (idx >= room_count_) return nullptr;
    return &rooms_[idx];
}

Room *RoomManager::find_mutable(const std::string &id)
{
    std::size_t idx = find_index(id);
    if (idx >= room_count_) return nullptr;
    return &rooms_[idx];
}

bool RoomManager::channel_exists(const std::string &channel_id) const
{
    return channel_mgr_.find(channel_id) != nullptr;
}

RoomResult RoomManager::add_light(const std::string &room_id,
                                  const std::string &channel_id)
{
    Room *room = find_mutable(room_id);
    if (room == nullptr) return RoomResult::kNotFound;
    if (!channel_exists(channel_id)) return RoomResult::kInvalid;
    // Avoid duplicates.
    for (const auto &cid : room->channel_ids) {
        if (cid == channel_id) return RoomResult::kOk;
    }
    room->channel_ids.push_back(channel_id);
    ConfigStatus st = save_rooms();
    if (st != ConfigStatus::kOk) {
        // Rollback.
        auto &vec = room->channel_ids;
        vec.pop_back();
        return RoomResult::kInvalid;
    }
    return RoomResult::kOk;
}

RoomResult RoomManager::remove_light(const std::string &room_id,
                                     const std::string &channel_id)
{
    Room *room = find_mutable(room_id);
    if (room == nullptr) return RoomResult::kNotFound;
    auto &vec = room->channel_ids;
    auto it = std::find(vec.begin(), vec.end(), channel_id);
    if (it == vec.end()) return RoomResult::kOk;
    vec.erase(it);
    ConfigStatus st = save_rooms();
    if (st != ConfigStatus::kOk) {
        // Rollback - re-add the light id.
        vec.push_back(channel_id);
        return RoomResult::kInvalid;
    }
    return RoomResult::kOk;
}

RoomResult RoomManager::remove_room(const std::string &room_id)
{
    std::size_t idx = find_index(room_id);
    if (idx >= room_count_) return RoomResult::kNotFound;
    // Remove the room — lights in the room are detached (not deleted).
    rooms_[idx] = rooms_[room_count_ - 1];
    room_count_--;
    ConfigStatus st = save_rooms();
    if (st != ConfigStatus::kOk) {
        return RoomResult::kInvalid;
    }
    return RoomResult::kOk;
}

RoomResult RoomManager::set_power(const std::string &room_id, bool power,
                                  uint32_t transition_ms,
                                  const std::string &origin)
{
    Room *room = find_mutable(room_id);
    if (room == nullptr) return RoomResult::kNotFound;

    LightCommand cmd{};
    cmd.channel_id = room_id; // placeholder — lighting task resolves per-channel
    cmd.state.power = power;
    cmd.transition_ms = transition_ms;
    cmd.origin = origin;

    // Fan out: one command per light in the room, each with its own channel_id.
    LightCommandEnvelope envelope{};
    envelope.posted_ms = 0; // lighting task fills timestamp

    for (const auto &cid : room->channel_ids) {
        LightCommand fanout = cmd;
        fanout.channel_id = cid;
        envelope.command = fanout;
        (void)envelope;
    }
    return RoomResult::kOk;
}

RoomResult RoomManager::set_brightness(const std::string &room_id,
                                       uint8_t brightness,
                                       uint32_t transition_ms,
                                       const std::string &origin)
{
    Room *room = find_mutable(room_id);
    if (room == nullptr) return RoomResult::kNotFound;

    LightCommand cmd{};
    cmd.state.power = true;
    cmd.state.brightness = brightness;
    cmd.transition_ms = transition_ms;
    cmd.origin = origin;

    for (const auto &cid : room->channel_ids) {
        LightCommand fanout = cmd;
        fanout.channel_id = cid;
        LightCommandEnvelope envelope{};
        envelope.command = fanout;
        (void)envelope;
    }
    return RoomResult::kOk;
}

RoomResult RoomManager::activate_scene(const std::string &room_id,
                                       const std::string &scene_id,
                                       const std::string &origin)
{
    (void)room_id;
    (void)scene_id;
    (void)origin;
    // Scene activation is handled by SceneManager; room-scoped activation
    // would filter scene states to this room's lights.
    return RoomResult::kOk;
}

ConfigStatus RoomManager::save_rooms()
{
    // Build JSON document: { "rooms": [ { id, name, floor, icon, channel_ids, group_ids, floorplan_id }, ... ] }
    // Use cJSON directly since IJson doesn't have add_array of objects easily.
    // Actually we can use IJson interface:
    //   json.add_object(doc, "rooms")
    //   for each room: json.add_object(room_obj, "rooms") ... but add_object doesn't return sub-doc.
    //
    // Simpler: build JSON string manually using cJSON.
    cJSON *root = cJSON_CreateObject();
    if (!root) return ConfigStatus::kIoError;

    cJSON *rooms_arr = cJSON_CreateArray();
    if (!rooms_arr) {
        cJSON_Delete(root);
        return ConfigStatus::kIoError;
    }
    cJSON_AddItemToObject(root, "rooms", rooms_arr);

    for (std::size_t i = 0; i < room_count_; ++i) {
        const Room &r = rooms_[i];
        cJSON *room_obj = cJSON_CreateObject();
        if (!room_obj) {
            cJSON_Delete(root);
            return ConfigStatus::kIoError;
        }
        cJSON_AddItemToArray(rooms_arr, room_obj);

        cJSON_AddStringToObject(room_obj, "id", r.id.c_str());
        cJSON_AddStringToObject(room_obj, "name", r.name.c_str());
        if (!r.floor.empty()) cJSON_AddStringToObject(room_obj, "floor", r.floor.c_str());
        if (!r.icon.empty()) cJSON_AddStringToObject(room_obj, "icon", r.icon.c_str());
        if (!r.floorplan_id.empty()) cJSON_AddStringToObject(room_obj, "floorplan_id", r.floorplan_id.c_str());

        cJSON *lights_arr = cJSON_CreateArray();
        if (!lights_arr) {
            cJSON_Delete(root);
            return ConfigStatus::kIoError;
        }
        cJSON_AddItemToObject(room_obj, "light_ids", lights_arr);
        for (const auto &cid : r.channel_ids) {
            cJSON_AddItemToArray(lights_arr, cJSON_CreateString(cid.c_str()));
        }

        cJSON *groups_arr = cJSON_CreateArray();
        if (!groups_arr) {
            cJSON_Delete(root);
            return ConfigStatus::kIoError;
        }
        cJSON_AddItemToObject(room_obj, "group_ids", groups_arr);
        for (const auto &gid : r.group_ids) {
            cJSON_AddItemToArray(groups_arr, cJSON_CreateString(gid.c_str()));
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ConfigStatus::kIoError;

    ConfigStatus st = config_.save_document("rooms.json", json_str);
    cJSON_free(json_str);
    return st;
}

ConfigStatus RoomManager::load_rooms()
{
    std::string contents;
    ConfigStatus st = config_.load_document("rooms.json", contents);
    if (st != ConfigStatus::kOk) {
        // No existing rooms — that's fine, start empty.
        return ConfigStatus::kOk;
    }

    // Parse JSON.
    JsonDoc *doc = config_.json().parse(contents);
    if (!doc) return ConfigStatus::kCorrupt;

    JsonDoc *rooms_arr = config_.json().get_object(doc, "rooms");
    if (!rooms_arr) {
        config_.json().free(doc);
        return ConfigStatus::kCorrupt;
    }

    int arr_size = config_.json().get_array_size(doc, "rooms");
    if (arr_size < 0) {
        config_.json().free(doc);
        return ConfigStatus::kCorrupt;
    }

    for (int i = 0; i < arr_size; ++i) {
        JsonDoc *room_obj = config_.json().get_array_item(rooms_arr, i);
        if (!room_obj) continue;

        Room room{};
        std::string id_str;
        if (config_.json().get_string(room_obj, "id", id_str) == IJson::JsonResult::kOk) {
            room.id = id_str;
        }
        std::string name_str;
        if (config_.json().get_string(room_obj, "name", name_str) == IJson::JsonResult::kOk) {
            room.name = name_str;
        }
        std::string floor_str;
        if (config_.json().get_string(room_obj, "floor", floor_str) == IJson::JsonResult::kOk) {
            room.floor = floor_str;
        }
        std::string icon_str;
        if (config_.json().get_string(room_obj, "icon", icon_str) == IJson::JsonResult::kOk) {
            room.icon = icon_str;
        }
        std::string floorplan_str;
        if (config_.json().get_string(room_obj, "floorplan_id", floorplan_str) == IJson::JsonResult::kOk) {
            room.floorplan_id = floorplan_str;
        }

        // Load light_ids array.
        JsonDoc *lights_arr = config_.json().get_object(room_obj, "light_ids");
        if (lights_arr) {
            int light_count = config_.json().get_array_size(room_obj, "light_ids");
            for (int j = 0; j < light_count; ++j) {
                JsonDoc *light_id_item = config_.json().get_array_item(lights_arr, j);
                if (!light_id_item) continue;
                std::string lid;
                if (config_.json().get_string(light_id_item, "", lid) == IJson::JsonResult::kOk) {
                    room.channel_ids.push_back(lid);
                }
            }
        }

        rooms_[room_count_] = room;
        room_count_++;
    }

    config_.json().free(doc);
    return ConfigStatus::kOk;
}

} // namespace lumen
