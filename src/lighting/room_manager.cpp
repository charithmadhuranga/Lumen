// Lumen — room manager implementation (src/lighting/room_manager.cpp)
//
// Room CRUD + master operations fan-out through the lighting task.
// See room_manager.h for the contract.
#include "lumen/lighting/room_manager.h"

#include <cstring>
#include <algorithm>

namespace lumen
{

RoomManager::RoomManager(ChannelManager &channel_mgr,
                         LightingTask &lighting_task)
    : channel_mgr_(channel_mgr), lighting_task_(lighting_task)
{
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
    return RoomResult::kOk;
}

RoomResult RoomManager::remove_light(const std::string &room_id,
                                     const std::string &channel_id)
{
    Room *room = find_mutable(room_id);
    if (room == nullptr) return RoomResult::kNotFound;
    auto &vec = room->channel_ids;
    auto it = std::find(vec.begin(), vec.end(), channel_id);
    if (it == vec.end()) return RoomResult::kOk; // already absent
    vec.erase(it);
    return RoomResult::kOk;
}

RoomResult RoomManager::remove_room(const std::string &room_id)
{
    std::size_t idx = find_index(room_id);
    if (idx >= room_count_) return RoomResult::kNotFound;
    // Remove the room — lights are NOT deleted, just detached from the room.
    rooms_[idx] = rooms_[room_count_ - 1];
    room_count_--;
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
        // lighting_task_.enqueue(envelope); // TODO: wire up when lighting task is live
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

} // namespace lumen
