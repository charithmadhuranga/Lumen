// Lumen — logical channel model (include/lumen/lighting/channel.h)
//
// Every light is a logical channel (SPEC §8.1). Channels are hardware-
// independent: they reference drivers through `device_id`/`channel` and
// resolve through the hardware manager. `src/lighting/` contains zero GPIO
// or pin knowledge (AGENTS.md §4.1).
//
// State triple kept separate:
//   requested  — user intent (latest command target)
//   applied    — what the engine last wrote to hardware (moves during a
//                transition/effect)
//   actual     — read-back/feedback where available
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lumen/lighting/color_engine.h"

namespace lumen
{

// Supported logical types (SPEC §8.2).
enum class LightType : uint8_t
{
    kSwitch = 0,
    kDimmer,
    kRgb,
    kRgbw,
    kCct,
    kAddressableRgb,
    kAddressableRgbw,
};

const char *light_type_name(LightType type);
LightType light_type_from_string(const char *name);
bool light_type_known(const char *name);

// Number of driver outputs a light type consumes (dimmer=1, rgb=3, ...).
uint8_t light_type_output_count(LightType type);

// Driver reference: which physical driver controls this channel.
struct DriverRef
{
    std::string type;       // "pca9685", "ledc", "relay", "rmt"
    std::string device_id;  // hardware-manager id, e.g. "pca9685_0", "relay1"
    int32_t channel = 0;    // first output index on that device
};

// Per-channel operating limits.
struct ChannelLimits
{
    uint8_t minimum = 0;    // 0..100 brightness floor when on
    uint8_t maximum = 100;  // 0..100 brightness ceiling
    uint16_t color_temperature_min = 2700; // kelvin (warm white LED)
    uint16_t color_temperature_max = 6500; // kelvin (cool white LED)
};

// Startup behavior (SPEC §8.1).
enum class StartupBehavior : uint8_t
{
    kRestore = 0, // restore last applied state on boot
    kOn,
    kOff,
};

StartupBehavior startup_behavior_from_string(const char *name);
const char *startup_behavior_name(StartupBehavior behavior);

// One logical lighting channel.
struct Channel
{
    std::string id;                // unique channel id
    std::string name;
    LightType type = LightType::kDimmer;
    std::string room_id;
    DriverRef driver;
    ChannelLimits limits;
    ChannelCalibration calibration;
    uint32_t transition_ms = 500;  // default transition duration
    StartupBehavior startup = StartupBehavior::kRestore;

    // State triple.
    LightState requested{}; // user intent
    LightState applied{};   // last commanded to hardware
    LightState actual{};    // read-back (unused unless driver reports)
};

// Channel registry: fixed-size, no heap growth of the table itself.
class ChannelManager
{
  public:
    static constexpr std::size_t kMaxChannels = 64;
    static constexpr std::size_t kNotFound = kMaxChannels;

    ChannelManager() = default;

    // Register a channel. Returns false when the registry is full or the id
    // is already registered.
    bool register_channel(const Channel &channel);

    // Remove a channel. Slots after it shift down (callers holding slot
    // indexes must re-resolve). Returns false when unknown.
    bool remove_channel(const std::string &id);

    // Look up a channel by id. Returns nullptr when unknown.
    const Channel *find(const std::string &id) const;
    Channel *find_mutable(const std::string &id);

    // Slot access (0..channel_count()-1).
    std::size_t index_of(const std::string &id) const;
    Channel &at(std::size_t index) { return channels_[index]; }
    const Channel &at(std::size_t index) const { return channels_[index]; }

    std::size_t channel_count() const { return channel_count_; }

    // Apply a state patch to a channel's requested state (full-state merge).
    // Returns false when the channel is unknown or the patch fails type-aware
    // validation.
    bool apply_patch(const std::string &channel_id, const LightState &patch,
                     LightState &out_requested);

    bool get_requested(const std::string &channel_id, LightState &out) const;

    // Mark a channel's applied state (called after a hardware write).
    bool set_applied(const std::string &channel_id, const LightState &applied);

    // Mark a channel available/unavailable (driver missing/invalid).
    bool set_available(const std::string &channel_id, bool available);
    bool is_available(const std::string &channel_id) const;

  private:
    Channel channels_[kMaxChannels]{};
    bool available_[kMaxChannels]{};
    std::size_t channel_count_ = 0;
};

// Driver-agnostic output levels for one channel. All levels are 16-bit
// linear (0 = off, 65535 = full); adapters scale to their device.
struct DriverDuty
{
    uint16_t duty = 0;              // single-output level (dimmer/switch)
    uint16_t channel_duty[5] = {};  // per-output level (R,G,B,W | warm,cool)
    uint8_t channel_count = 0;      // valid channel_duty entries
    bool relay_on = false;          // switch/relay state
};

// Translate a normalized LightState into output levels using the colour
// engine (perceptual dimming curve, calibration, CCT warm/cool mixing).
DriverDuty state_to_duties(const Channel &channel, const LightState &state,
                           const ChannelCalibration &cal);

// Normalise a state for a channel: type-aware clamp + channel limits.
LightState normalize_for_channel(const Channel &channel, const LightState &state);

} // namespace lumen
