// Lumen — group manager (include/lumen/lighting/group_manager.h)
//
// Groups organize lights across rooms with an aggregation policy
// (SPEC §9.2). Group operations fan out through the lighting task.
//
// Groups are persisted transactionally through the config manager.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "lumen/lighting/channel.h"
#include "lumen/lighting/lighting_task.h"
#include "lumen/core/config_manager.hpp"

namespace lumen
{

// Result of group operations.
enum class GroupResult : uint8_t
{
    kOk = 0,
    kNotFound,
    kDuplicate,
    kInvalid,
    kCapacity,
};

// Aggregation policy for mixed-type groups.
enum class AggregationPolicy : uint8_t
{
    kAverage = 0,   // average brightness across members
    kMax = 1,       // max brightness across members
    kMin = 2,       // min brightness across members
};

// A group of lights that may span multiple rooms.
struct Group
{
    std::string id;                // unique group id
    std::string name;              // human-readable name
    std::string room_id;           // optional owning room
    std::vector<std::string> channel_ids; // lights in this group
    AggregationPolicy policy = AggregationPolicy::kAverage;
};

// Group manager: CRUD + group commands fan-out through the lighting task.
// All mutations are persisted transactionally via ConfigManager.
class GroupManager
{
  public:
    static constexpr std::size_t kMaxGroups = 32;

    GroupManager(ChannelManager &channel_mgr, LightingTask &lighting_task,
                 ConfigManager &config);

    GroupResult register_group(const Group &group);

    const Group *find(const std::string &id) const;
    Group *find_mutable(const std::string &id);

    std::size_t group_count() const { return group_count_; }

    GroupResult add_light(const std::string &group_id,
                          const std::string &channel_id);
    GroupResult remove_light(const std::string &group_id,
                             const std::string &channel_id);
    GroupResult remove_group(const std::string &group_id);

    // Group power: fan out to all members.
    GroupResult set_power(const std::string &group_id, bool power,
                          uint32_t transition_ms = 0,
                          const std::string &origin = "group",
                          const std::string &command_id = "");

    // Group brightness: adjusts every member's brightness, preserving color.
    GroupResult set_brightness(const std::string &group_id, uint8_t brightness,
                               uint32_t transition_ms = 0,
                               const std::string &origin = "group",
                               const std::string &command_id = "");

    // Group color: applies only to RGB-capable members.
    GroupResult set_color(const std::string &group_id, uint8_t r, uint8_t g,
                          uint8_t b, uint32_t transition_ms = 0,
                          const std::string &origin = "group",
                          const std::string &command_id = "");

    // Group CCT: applies only to CCT-capable members.
    GroupResult set_cct(const std::string &group_id, uint16_t kelvin,
                        uint32_t transition_ms = 0,
                        const std::string &origin = "group",
                        const std::string &command_id = "");

    // Activate a scene scoped to this group.
    GroupResult activate_scene(const std::string &group_id,
                               const std::string &scene_id,
                               const std::string &origin = "group",
                               const std::string &command_id = "");

    // Aggregate state for UI: on-count and mean brightness.
    bool aggregate_state(const std::string &group_id,
                         uint8_t &on_count_out,
                         uint8_t &mean_brightness_out) const;

  private:
    ChannelManager &channel_mgr_;
    LightingTask &lighting_task_;
    ConfigManager &config_;

    Group groups_[kMaxGroups]{};
    std::size_t group_count_ = 0;

    std::size_t find_index(const std::string &id) const;
    bool channel_exists(const std::string &channel_id) const;

    // Persist all groups to config as JSON.
    ConfigStatus save_groups();
    // Load all groups from config JSON.
    ConfigStatus load_groups();
};

} // namespace lumen
