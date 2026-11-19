// Lumen — channel manager implementation (src/lighting/channel_manager.cpp)
//
// Logical channel registry with state triple (requested/applied/actual) and
// type-aware validation (SPEC §8.1, §8.3). Pure C++17, no I/O, no heap
// beyond the fixed registry.
#include "lumen/lighting/channel.h"

#include <cstring>
#include <algorithm>

namespace lumen
{

namespace
{

constexpr char kTypeSwitch[] = "switch";
constexpr char kTypeDimmer[] = "dimmer";
constexpr char kTypeRgb[] = "rgb";
constexpr char kTypeRgbw[] = "rgbw";
constexpr char kTypeCct[] = "cct";
constexpr char kTypeAddressableRgb[] = "addressable_rgb";
constexpr char kTypeAddressableRgbw[] = "addressable_rgbw";

} // namespace

const char *light_type_name(LightType type)
{
    switch (type) {
    case LightType::kSwitch: return kTypeSwitch;
    case LightType::kDimmer: return kTypeDimmer;
    case LightType::kRgb: return kTypeRgb;
    case LightType::kRgbw: return kTypeRgbw;
    case LightType::kCct: return kTypeCct;
    case LightType::kAddressableRgb: return kTypeAddressableRgb;
    case LightType::kAddressableRgbw: return kTypeAddressableRgbw;
    }
    return "unknown";
}

LightType light_type_from_string(const char *name)
{
    if (name == nullptr) return LightType::kDimmer;
    if (std::strcmp(name, kTypeSwitch) == 0) return LightType::kSwitch;
    if (std::strcmp(name, kTypeDimmer) == 0) return LightType::kDimmer;
    if (std::strcmp(name, kTypeRgb) == 0) return LightType::kRgb;
    if (std::strcmp(name, kTypeRgbw) == 0) return LightType::kRgbw;
    if (std::strcmp(name, kTypeCct) == 0) return LightType::kCct;
    if (std::strcmp(name, kTypeAddressableRgb) == 0)
        return LightType::kAddressableRgb;
    if (std::strcmp(name, kTypeAddressableRgbw) == 0)
        return LightType::kAddressableRgbw;
    return LightType::kDimmer; // safe default
}

StartupBehavior startup_behavior_from_string(const char *name)
{
    if (name == nullptr) return StartupBehavior::kRestore;
    if (std::strcmp(name, "restore") == 0) return StartupBehavior::kRestore;
    if (std::strcmp(name, "on") == 0) return StartupBehavior::kOn;
    if (std::strcmp(name, "off") == 0) return StartupBehavior::kOff;
    return StartupBehavior::kRestore; // safe default
}

bool ChannelManager::register_channel(const Channel &channel)
{
    if (channel_count_ >= kMaxChannels) {
        return false;
    }
    if (find_index(channel.id) < channel_count_) {
        return false; // duplicate id
    }
    channels_[channel_count_] = channel;
    available_[channel_count_] = true;
    channel_count_++;
    return true;
}

std::size_t ChannelManager::find_index(const std::string &id) const
{
    for (std::size_t i = 0; i < channel_count_; ++i) {
        if (channels_[i].id == id) {
            return i;
        }
    }
    return channel_count_; // sentinel = not found
}

const Channel *ChannelManager::find(const std::string &id) const
{
    std::size_t idx = find_index(id);
    if (idx >= channel_count_) return nullptr;
    return &channels_[idx];
}

Channel *ChannelManager::find_mutable(const std::string &id)
{
    std::size_t idx = find_index(id);
    if (idx >= channel_count_) return nullptr;
    return &channels_[idx];
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

    // Merge patch into requested state. The patch carries the full desired
    // state for the fields the caller cares about; unspecified fields keep
    // their current value. A brightness/color value of 0 is a legitimate
    // target (off), so callers must explicitly set fields they want changed
    // and leave others at their current requested value.
    LightState merged = ch->requested;
    merged.power = patch.power;
    merged.brightness = patch.brightness;
    merged.red = patch.red;
    merged.green = patch.green;
    merged.blue = patch.blue;
    merged.white = patch.white;
    merged.color_temperature = patch.color_temperature;

    // Normalize against channel limits + type.
    ChannelCalibration cal{}; // identity calibration
    merged = normalize_state(merged, light_type_name(ch->type), cal);

    // Clamp brightness to channel limits.
    merged.brightness =
        clamp(merged.brightness, ch->limits.minimum, ch->limits.maximum);

    ch->requested = merged;
    out_requested = merged;
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

bool ChannelManager::set_available(const std::string &channel_id,
                                   bool available)
{
    Channel *ch = find_mutable(channel_id);
    if (ch == nullptr) return false;
    available_[find_index(channel_id)] = available;
    return true;
}

bool ChannelManager::is_available(const std::string &channel_id) const
{
    std::size_t idx = find_index(channel_id);
    if (idx >= channel_count_) return false;
    return available_[idx];
}

// ========================================================================
// state_to_duties — translate normalized LightState to driver duty values
// ========================================================================

DriverDuty state_to_duties(const Channel &channel,
                           const LightState &state,
                           const ChannelCalibration &cal)
{
    DriverDuty duty{};

    if (!state.power) {
        duty.duty = 0;
        duty.relay_on = false;
        duty.channel_count = 0;
        return duty;
    }

    // Brightness-limited RGBW color (already normalized by caller).
    uint8_t bri = state.brightness; // 0..100

    switch (channel.type) {
    case LightType::kSwitch:
    case LightType::kDimmer: {
        // Single channel: brightness -> duty 0..65535.
        uint32_t d = (static_cast<uint32_t>(bri) * 65535) / 100;
        duty.duty = static_cast<uint16_t>(clamp(d, uint32_t(0), uint32_t(65535)));
        duty.channel_count = 0;
        break;
    }
    case LightType::kRgb: {
        // Use the state's red/green/blue directly, brightness-compensated.
        uint16_t rd = (static_cast<uint32_t>(apply_brightness(state.red, bri)) * 4095) / 255;
        uint16_t gd = (static_cast<uint32_t>(apply_brightness(state.green, bri)) * 4095) / 255;
        uint16_t bd = (static_cast<uint32_t>(apply_brightness(state.blue, bri)) * 4095) / 255;
        duty.channel_duty[0] = static_cast<uint16_t>(clamp(rd, static_cast<uint16_t>(0), static_cast<uint16_t>(4095)));
        duty.channel_duty[1] = static_cast<uint16_t>(clamp(gd, static_cast<uint16_t>(0), static_cast<uint16_t>(4095)));
        duty.channel_duty[2] = static_cast<uint16_t>(clamp(bd, static_cast<uint16_t>(0), static_cast<uint16_t>(4095)));
        duty.channel_count = 3;
        break;
    }
    case LightType::kRgbw: {
        uint16_t rd = (static_cast<uint32_t>(apply_brightness(state.red, bri)) * 4095) / 255;
        uint16_t gd = (static_cast<uint32_t>(apply_brightness(state.green, bri)) * 4095) / 255;
        uint16_t bd = (static_cast<uint32_t>(apply_brightness(state.blue, bri)) * 4095) / 255;
        uint16_t wd = (static_cast<uint32_t>(apply_brightness(state.white, bri)) * 4095) / 255;
        duty.channel_duty[0] = static_cast<uint16_t>(clamp(rd, static_cast<uint16_t>(0), static_cast<uint16_t>(4095)));
        duty.channel_duty[1] = static_cast<uint16_t>(clamp(gd, static_cast<uint16_t>(0), static_cast<uint16_t>(4095)));
        duty.channel_duty[2] = static_cast<uint16_t>(clamp(bd, static_cast<uint16_t>(0), static_cast<uint16_t>(4095)));
        duty.channel_duty[3] = static_cast<uint16_t>(clamp(wd, static_cast<uint16_t>(0), static_cast<uint16_t>(4095)));
        duty.channel_count = 4;
        break;
    }
    case LightType::kCct: {
        // CCT: kelvin -> RGB mix; brightness splits across warm/cool channels.
        Rgb8 rgb = kelvin_to_rgb(state.color_temperature != 0
                                     ? state.color_temperature
                                     : 2700);
        uint16_t rd = (static_cast<uint32_t>(apply_brightness(rgb.r, bri)) * 4095) / 255;
        uint16_t bd_ = (static_cast<uint32_t>(apply_brightness(rgb.b, bri)) * 4095) / 255;
        duty.channel_duty[0] = static_cast<uint16_t>(clamp(rd, static_cast<uint16_t>(0), static_cast<uint16_t>(4095))); // warm
        duty.channel_duty[1] = static_cast<uint16_t>(clamp(bd_, static_cast<uint16_t>(0), static_cast<uint16_t>(4095))); // cool
        duty.channel_count = 2;
        break;
    }
    case LightType::kAddressableRgb:
    case LightType::kAddressableRgbw: {
        // Addressable: per-LED handled separately by the effects engine;
        // here just set a global duty placeholder.
        duty.duty = (static_cast<uint32_t>(bri) * 65535) / 100;
        duty.channel_count = 0;
        break;
    }
    default:
        duty.duty = 0;
        duty.channel_count = 0;
        break;
    }

    return duty;
}

} // namespace lumen
