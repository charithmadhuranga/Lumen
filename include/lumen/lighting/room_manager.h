// Lumen — room manager (include/lumen/lighting/room_manager.h)
//
// Rooms organize lights into named groups with optional floor and icon
// metadata (SPEC §9.1). Room-level operations (master power/brightness) fan
// out through the lighting engine.
//
// Rooms are persisted transactionally through the config manager; deleting a
// room detaches its lights without deleting the lights themselves.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "lumen/lighting/channel.h"
#include "lumen/lighting/lighting_task.h"

namespace lumen
{

// Result of room operations.
enum class RoomResult : uint8_t
{
    kOk = 0,
    kNotFound,       // room id not found
    kDuplicate,      // duplicate id
    kInvalid,        // validation failure (e.g. unknown channel id)
    kCapacity,       // room registry full
};

// A room grouping lights together.
struct Room
{
    std::string id;                // unique room id
    std::string name;              // human-readable name
    std::string floor;             // optional floor name
    std::string icon;              // optional icon name
    std::vector<std::string> channel_ids; // lights in this room
    std::vector<std::string> group_ids;   // groups this room belongs to
    std::string floorplan_id;      // optional floor-plan reference (Phase 8)
};

// Room manager: CRUD + master operations fan-out through the lighting task.
class RoomManager
{
  public:
    static constexpr std::size_t kMaxRooms = 32;

    RoomManager(ChannelManager &channel_mgr, LightingTask &lighting_task);

    // Register a room. Returns kDuplicate if id exists, kCapacity if full.
    RoomResult register_room(const Room &room);

    // Look up a room by id.
    const Room *find(const std::string &id) const;
    Room *find_mutable(const std::string &id);

    std::size_t room_count() const { return room_count_; }

    // Add a light channel to a room.
    RoomResult add_light(const std::string &room_id,
                         const std::string &channel_id);

    // Remove a light channel from a room (does not delete the light).
    RoomResult remove_light(const std::string &room_id,
                            const std::string &channel_id);

    // Remove a room entirely. Lights in the room are detached (not deleted).
    RoomResult remove_room(const std::string &room_id);

    // Master power: fan out power command to all lights in the room.
    RoomResult set_power(const std::string &room_id, bool power,
                         uint32_t transition_ms = 0,
                         const std::string &origin = "room");

    // Master brightness: fan out brightness command to all lights in the room.
    RoomResult set_brightness(const std::string &room_id, uint8_t brightness,
                              uint32_t transition_ms = 0,
                              const std::string &origin = "room");

    // Activate a scene scoped to this room (fan-out shortcut).
    RoomResult activate_scene(const std::string &room_id,
                              const std::string &scene_id,
                              const std::string &origin = "room");

  private:
    ChannelManager &channel_mgr_;
    LightingTask &lighting_task_;

    Room rooms_[kMaxRooms]{};
    std::size_t room_count_ = 0;

    std::size_t find_index(const std::string &id) const;
    bool channel_exists(const std::string &channel_id) const;
};

} // namespace lumen
