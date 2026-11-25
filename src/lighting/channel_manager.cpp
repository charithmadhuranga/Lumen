// Lumen — channel manager implementation (src/lighting/channel_manager.cpp)
//
// Logical channel registry with state triple (requested/applied/actual) and
// type-aware validation (SPEC §8.1, §8.3), plus the state -> output-level
// translation used by the lighting engine. Pure C++17, no I/O.
#include "lumen/lighting/channel.h"

#include <algorithm>
#include <cstring>

namespace lumen
{

namespace
{

struct TypeName
{
    LightType type;
    const char *name;
    uint8_t outputs;
};

constexpr TypeName kTypes[] = {
    {LightType::kSwitch, "switch", 1},
    {LightType::kDimmer, "dimmer", 1},
    {LightType::kRgb, "rgb", 3},
    {LightType::kRgbw, "rgbw", 4},
    {LightType::kCct, "cct", 2},
    {LightType::kAddressableRgb, "addressable_rgb", 1},
    {LightType::kAddressableRgbw, "addressable_rgbw", 1},
};

} // namespace

const char *light_type_name(LightType type)
{
    for (const auto &t : kTypes) {
        if (t.type == type) {
            return t.name;
        }
    }
    return "unknown";
}

LightType light_type_from_string(const char *name)
{
    if (name != nullptr) {
        for (const auto &t : kTypes) {
            if (std::strcmp(name, t.name) == 0) {
                return t.type;
            }
        }
    }
    return LightType::kDimmer; // safe default
}

bool light_type_known(const char *name)
{
    if (name == nullptr) {
        return false;
    }
    for (const auto &t : kTypes) {
        if (std::strcmp(name, t.name) == 0) {
            return true;
        }
    }
    return false;
}

uint8_t light_type_output_count(LightType type)
{
    for (const auto &t : kTypes) {
        if (t.type == type) {
            return t.outputs;
        }
    }
    return 1;
}

StartupBehavior startup_behavior_from_string(const char *name)
{
    if (name == nullptr) return StartupBehavior::kRestore;
    if (std::strcmp(name, "on") == 0) return StartupBehavior::kOn;
    if (std::strcmp(name, "off") == 0) return StartupBehavior::kOff;
    return StartupBehavior::kRestore; // safe default
}

const char *startup_behavior_name(StartupBehavior behavior)
{
    switch (behavior) {
    case StartupBehavior::kOn: return "on";
    case StartupBehavior::kOff: return "off";
    case StartupBehavior::kRestore: break;
    }
    return "restore";
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

bool ChannelManager::register_channel(const Channel &channel)
{
    if (channel_count_ >= kMaxChannels || channel.id.empty()) {
        return false;
    }
    if (index_of(channel.id) != kNotFound) {
        return false; // duplicate id
    }
    channels_[channel_count_] = channel;
    available_[channel_count_] = true;
    channel_count_++;
    return true;
}

bool ChannelManager::remove_channel(const std::string &id)
{
    const std::size_t idx = index_of(id);
    if (idx == kNotFound) {
        return false;
    }
    for (std::size_t i = idx; i + 1 < channel_count_; ++i) {
        channels_[i] = channels_[i + 1];
        available_[i] = available_[i + 1];
    }
    channel_count_--;
    channels_[channel_count_] = Channel{};
    available_[channel_count_] = false;
    return true;
}

std::size_t ChannelManager::index_of(const std::string &id) const
{
    for (std::size_t i = 0; i < channel_count_; ++i) {
        if (channels_[i].id == id) {
            return i;
        }
    }
    return kNotFound;
}

const Channel *ChannelManager::find(const std::string &id) const
{
    const std::size_t idx = index_of(id);
    return idx == kNotFound ? nullptr : &channels_[idx];
}

Channel *ChannelManager::find_mutable(const std::string &id)
{
    const std::size_t idx = index_of(id);
    return idx == kNotFound ? nullptr : &channels_[idx];
}

bool ChannelManager::apply_patch(const std::string &channel_id,
                                 const LightState &patch,
                                 LightState &out_requested)
{
    Channel *ch = find_mutable(channel_id);
    if (ch == nullptr) return false;
    if (ch->type != LightType::kSwitch &&
        !validate_state_for_type(patch, light_type_name(ch->type))) {
        return false; // unsupported property for this type
    }
    ch->requested = normalize_for_channel(*ch, patch);
    out_requested = ch->requested;
    return true;
}

bool ChannelManager::get_requested(const std::string &channel_id,
                                   LightState &out) const
{
    const Channel *ch = find(channel_id);
    if (ch == nullptr) return false;
    out = ch->requested;
    return true;
}

bool ChannelManager::set_applied(const std::string &channel_id,
                                 const LightState &applied)
{
    Channel *ch = find_mutable(channel_id);
    if (ch == nullptr) return false;
    ch->applied = applied;
    return true;
}

bool ChannelManager::set_available(const std::string &channel_id, bool available)
{
    const std::size_t idx = index_of(channel_id);
    if (idx == kNotFound) return false;
    available_[idx] = available;
    return true;
}

bool ChannelManager::is_available(const std::string &channel_id) const
{
    const std::size_t idx = index_of(channel_id);
    return idx != kNotFound && available_[idx];
}

// ---------------------------------------------------------------------------
// Normalisation and output translation
// ---------------------------------------------------------------------------

LightState normalize_for_channel(const Channel &channel, const LightState &state)
{
    const ChannelCalibration identity{};
    LightState out = normalize_state(state, light_type_name(channel.type), identity);

    if (channel.type == LightType::kSwitch) {
        out.brightness = out.power ? 100 : 0;
    }
    if (channel.type == LightType::kCct) {
        uint16_t k = out.color_temperature != 0 ? out.color_temperature
                                                : channel.limits.color_temperature_min;
        out.color_temperature = clamp(k, channel.limits.color_temperature_min,
                                      channel.limits.color_temperature_max);
    }
    if (out.power && channel.type != LightType::kSwitch) {
        const uint8_t lo = std::max<uint8_t>(channel.limits.minimum, 1);
        out.brightness = clamp(out.brightness, lo, channel.limits.maximum);
    }
    return out;
}

DriverDuty state_to_duties(const Channel &channel, const LightState &state,
                           const ChannelCalibration &cal)
{
    DriverDuty duty{};
    duty.channel_count = light_type_output_count(channel.type);
    if (!state.power || (channel.type != LightType::kSwitch && state.brightness == 0)) {
        // Everything off (channel_duty already zero).
        duty.relay_on = false;
        duty.duty = 0;
        return duty;
    }

    const uint16_t level = perceptual_level(state.brightness);
    const Rgbw8 color = apply_calibration({state.red, state.green, state.blue, state.white}, cal);

    switch (channel.type) {
    case LightType::kSwitch:
        duty.relay_on = true;
        duty.duty = 65535;
        duty.channel_duty[0] = 65535;
        break;
    case LightType::kDimmer:
        duty.relay_on = true;
        duty.duty = level;
        duty.channel_duty[0] = level;
        break;
    case LightType::kRgb:
        duty.relay_on = true;
        duty.channel_duty[0] = scale_component(color.r, level);
        duty.channel_duty[1] = scale_component(color.g, level);
        duty.channel_duty[2] = scale_component(color.b, level);
        break;
    case LightType::kRgbw:
        duty.relay_on = true;
        duty.channel_duty[0] = scale_component(color.r, level);
        duty.channel_duty[1] = scale_component(color.g, level);
        duty.channel_duty[2] = scale_component(color.b, level);
        duty.channel_duty[3] = scale_component(color.w, level);
        break;
    case LightType::kCct: {
        // Two-channel tunable white: linear warm/cool mix across the strip's
        // kelvin range, each scaled by the perceptual level.
        const uint16_t kmin = channel.limits.color_temperature_min;
        const uint16_t kmax = channel.limits.color_temperature_max;
        const uint16_t k = clamp(state.color_temperature != 0 ? state.color_temperature : kmin,
                                 kmin, kmax);
        const uint32_t span = (kmax > kmin) ? static_cast<uint32_t>(kmax - kmin) : 1U;
        const uint32_t cool_ratio = (static_cast<uint32_t>(k - kmin) * 1000U) / span; // 0..1000
        const uint32_t warm = (static_cast<uint32_t>(level) * (1000U - cool_ratio)) / 1000U;
        const uint32_t cool = (static_cast<uint32_t>(level) * cool_ratio) / 1000U;
        duty.relay_on = true;
        duty.channel_duty[0] = static_cast<uint16_t>(warm);
        duty.channel_duty[1] = static_cast<uint16_t>(cool);
        break;
    }
    case LightType::kAddressableRgb:
    case LightType::kAddressableRgbw:
        duty.relay_on = true;
        duty.duty = level;
        duty.channel_duty[0] = scale_component(color.r, level);
        duty.channel_duty[1] = scale_component(color.g, level);
        duty.channel_duty[2] = scale_component(color.b, level);
        duty.channel_duty[3] = scale_component(color.w, level);
        break;
    }
    return duty;
}

} // namespace lumen
