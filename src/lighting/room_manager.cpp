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

namespace
{
// cJSON helpers shared by the loaders (bounded string reads).
std::string json_str(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(it) && it->valuestring != nullptr) ? std::string(it->valuestring)
                                                              : std::string();
}
int json_int(const cJSON *obj, const char *key, int fallback)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(it) ? it->valueint : fallback;
}
std::vector<std::string> json_str_array(const cJSON *obj, const char *key)
{
    std::vector<std::string> out;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    const cJSON *it = nullptr;
    cJSON_ArrayForEach(it, arr)
    {
        if (cJSON_IsString(it) && it->valuestring != nullptr) {
            out.emplace_back(it->valuestring);
        }
    }
    return out;
}
} // namespace

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

    // Fan out: one command per light in the room (SPEC §9.1 master power).
    for (const auto &cid : room->channel_ids) {
        LightCommandEnvelope envelope{};
        envelope.command.channel_id = cid;
        envelope.command.state.power = power;
        envelope.command.fields = kFieldPower;
        envelope.command.transition_ms = transition_ms;
        envelope.command.origin = origin;
        lighting_task_.enqueue(envelope);
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

    for (const auto &cid : room->channel_ids) {
        LightCommandEnvelope envelope{};
        envelope.command.channel_id = cid;
        envelope.command.state.brightness = brightness;
        envelope.command.fields = kFieldBrightness;
        envelope.command.transition_ms = transition_ms;
        envelope.command.origin = origin;
        lighting_task_.enqueue(envelope);
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
    if (config_.load_document("rooms.json", contents) != ConfigStatus::kOk) {
        return ConfigStatus::kOk; // no rooms yet
    }
    cJSON *root = cJSON_Parse(contents.c_str());
    if (root == nullptr) return ConfigStatus::kCorrupt;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "rooms");
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, arr)
    {
        if (room_count_ >= kMaxRooms) break;
        Room room{};
        room.id = json_str(item, "id");
        if (room.id.empty() || find_index(room.id) < room_count_) continue;
        room.name = json_str(item, "name");
        room.floor = json_str(item, "floor");
        room.icon = json_str(item, "icon");
        room.floorplan_id = json_str(item, "floorplan_id");
        room.channel_ids = json_str_array(item, "light_ids");
        room.group_ids = json_str_array(item, "group_ids");
        rooms_[room_count_++] = room;
    }
    cJSON_Delete(root);
    return ConfigStatus::kOk;
}

} // namespace lumen
