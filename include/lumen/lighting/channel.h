// Lumen — logical channel model (include/lumen/lighting/channel.h)
//
// Every light is a logical channel (SPEC §8.1). Channels are hardware-
// independent: they reference drivers through `device_id`/`channel` and
// resolve through the hardware manager. `src/lighting/` contains zero GPIO
// or pin knowledge (AGENTS.md §4.1).
//
// State triple kept separate:
//   requested  — user intent (latest command)
//   applied    — what the engine last commanded to hardware
//   actual     — read-back/feedback where available
#pragma once

#include <cstdint>
#include <cstddef>
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

// Driver reference: which physical driver controls this channel.
struct DriverRef
{
    std::string type;       // e.g. "ledc", "pca9685", "relay", "rmt"
    std::string device_id;  // e.g. "pca9685_0", "ledc0"
    int32_t channel = 0;    // channel index on that device
};

// Per-channel operating limits.
struct ChannelLimits
{
    uint8_t minimum = 0;    // 0..100 brightness min
    uint8_t maximum = 100;  // 0..100 brightness max
    uint16_t color_temperature_min = 1000; // kelvin
    uint16_t color_temperature_max = 20000; // kelvin
};

// Startup behavior (SPEC §8.1).
enum class StartupBehavior : uint8_t
{
    kRestore = 0, // restore last applied state on boot
    kOn,
    kOff,
};

StartupBehavior startup_behavior_from_string(const char *name);

// One logical lighting channel.
struct Channel
{
    std::string id;                // unique channel id
    std::string name;
    LightType type = LightType::kDimmer;
    std::string room_id;
    DriverRef driver;
    ChannelLimits limits;
    uint32_t transition_ms = 500;  // default transition duration
    StartupBehavior startup = StartupBehavior::kRestore;

    // State triple.
    LightState requested{}; // user intent
    LightState applied{};   // last commanded to hardware
    LightState actual{};    // read-back (unused unless driver reports)
};

// Channel registry: fixed-size, no heap growth.
class ChannelManager
{
  public:
    static constexpr std::size_t kMaxChannels = 64;

    ChannelManager() = default;

    // Register a channel. Returns false when the registry is full or the id
    // is already registered.
    bool register_channel(const Channel &channel);

    // Look up a channel by id. Returns nullptr when unknown.
    const Channel *find(const std::string &id) const;
    Channel *find_mutable(const std::string &id);

    // All channels (read-only snapshot).
    std::size_t channel_count() const { return channel_count_; }

    // Apply a state patch to a channel's requested state. Returns false when
    // the channel is unknown or the patch fails type-aware validation.
    bool apply_patch(const std::string &channel_id, const LightState &patch,
                     LightState &out_requested);

    // Get a channel's requested state.
    bool get_requested(const std::string &channel_id, LightState &out) const;

    // Mark a channel's applied state (called by the lighting task after
    // hardware write succeeds).
    bool set_applied(const std::string &channel_id, const LightState &applied);

    // Mark a channel available/unavailable (driver missing/invalid).
    bool set_available(const std::string &channel_id, bool available);

    bool is_available(const std::string &channel_id) const;

  private:
    Channel channels_[kMaxChannels]{};
    bool available_[kMaxChannels]{};
    std::size_t channel_count_ = 0;

    std::size_t find_index(const std::string &id) const;
};

// Convert a Channel's requested LightState to driver duty values.
//
// LEDC/dimmer: brightness -> duty (0..65535).
// PCA9685 RGB/RGBW: per-channel duty (0..4095).
// Relay: power -> on/off.
// CCT: kelvin + brightness -> duty on warm/cool channels.
// Addressable: per-LED RGB/RGBW.
struct DriverDuty
{
    uint16_t duty = 0;          // generic single-channel duty (0..65535)
    uint16_t channel_duty[16] = {}; // per-PCA9685-channel duty (0..4095)
    uint8_t channel_count = 0;  // how many channel_duty entries are valid
    bool relay_on = false;
};

// Translate a normalized LightState into driver-specific duty values using
// the color engine. `channel` supplies limits and calibration.
DriverDuty state_to_duties(const Channel &channel,
                           const LightState &state,
                           const ChannelCalibration &cal);

} // namespace lumen
