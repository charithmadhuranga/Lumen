// Lumen — light configuration store (SPEC §8.1, §19.2).
//
// lights.json       : channel definitions (id, name, type, driver ref,
//                     limits, calibration, startup behaviour)
// light_state.json  : last requested state per channel, written debounced
//                     by the lighting service so "restore" survives reboot
//
// Also the single place where a candidate channel definition is validated
// (API create/update, config load): id format, known type, driver type,
// output range and output conflicts with other channels.
#pragma once

#include <string>
#include <vector>

#include "core/cJSON.h"
#include "lumen/core/config_manager.hpp"
#include "lumen/hardware/board_profile.h"
#include "lumen/lighting/lighting_task.h"

namespace lumen
{
namespace light_store
{

constexpr const char *kLightsDoc = "lights.json";
constexpr const char *kStateDoc = "light_state.json";

// Register both documents with the config manager (call once at boot).
void register_documents(ConfigManager &config);

// Valid entity id: 1..48 chars of [a-z0-9_-].
bool valid_id(const std::string &id);

// Parse one channel definition. `error` gets a human-readable reason.
bool channel_from_json(const cJSON *obj, Channel &out, std::string &error);

// Serialise a channel definition (+ state when include_state).
cJSON *channel_to_json(const Channel &ch, bool include_state);

// Serialise a LightState in API shape (camelCase colorTemperature).
cJSON *state_to_json(const LightState &state);

// Parse API light state fields into a LightCommand (partial update).
// Returns false with `error` set when a value is out of range.
bool command_from_json(const cJSON *obj, LightCommand &cmd, std::string &error);

// Validate a candidate against the active board profile and the other
// registered channels (output overlap). `ignore_id` excludes the channel
// being updated from the overlap check.
bool validate_against(const Channel &candidate, const BoardProfile &profile,
                      const ChannelManager &channels, const std::string &ignore_id,
                      std::string &error);

ConfigStatus load_lights(ConfigManager &config, LightingTask &lighting);
ConfigStatus save_lights(ConfigManager &config, const LightingTask &lighting);

// Apply startup behaviour from light_state.json (restore/on/off).
ConfigStatus load_states(ConfigManager &config, LightingTask &lighting);
ConfigStatus save_states(ConfigManager &config, const LightingTask &lighting);

} // namespace light_store
} // namespace lumen
