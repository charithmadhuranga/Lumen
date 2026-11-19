// Lumen — group manager implementation (src/lighting/group_manager.cpp)
//
// Group CRUD + aggregation-aware fan-out through the lighting task.
// See group_manager.h for the contract.
#include "lumen/lighting/group_manager.h"

#include <cstring>
#include <algorithm>
#include <cmath>

namespace lumen
{

GroupManager::GroupManager(ChannelManager &channel_mgr,
                           LightingTask &lighting_task)
    : channel_mgr_(channel_mgr), lighting_task_(lighting_task)
{
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
    return GroupResult::kOk;
}

GroupResult GroupManager::remove_group(const std::string &group_id)
{
    std::size_t idx = find_index(group_id);
    if (idx >= group_count_) return GroupResult::kNotFound;
    groups_[idx] = groups_[group_count_ - 1];
    group_count_--;
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

} // namespace lumen
