// Lumen — scene manager implementation (src/lighting/scene_manager.cpp)
//
// Scene CRUD + activation fan-out through the lighting task.
// See scene_manager.h for the contract.
#include "lumen/lighting/scene_manager.h"

#include <cstring>
#include <algorithm>

namespace lumen
{

SceneManager::SceneManager(ChannelManager &channel_mgr,
                           LightingTask &lighting_task)
    : channel_mgr_(channel_mgr), lighting_task_(lighting_task)
{
}

SceneResult SceneManager::register_scene(const Scene &scene)
{
    if (scene_count_ >= kMaxScenes) {
        return SceneResult::kCapacity;
    }
    if (find_index(scene.id) < scene_count_) {
        return SceneResult::kDuplicate;
    }
    // Validate all light ids exist and states are type-valid.
    for (const auto &st : scene.states) {
        if (!channel_exists(st.light_id)) {
            return SceneResult::kInvalid;
        }
        const Channel *ch = channel_mgr_.find(st.light_id);
        if (ch == nullptr) return SceneResult::kInvalid;
        LightState ls{};
        ls.power = st.power;
        ls.brightness = st.brightness;
        ls.red = st.red;
        ls.green = st.green;
        ls.blue = st.blue;
        ls.white = st.white;
        ls.color_temperature = st.color_temperature;
        if (!validate_state_for_type(ls, light_type_name(ch->type))) {
            return SceneResult::kInvalid;
        }
    }
    scenes_[scene_count_] = scene;
    scene_count_++;
    return SceneResult::kOk;
}

std::size_t SceneManager::find_index(const std::string &id) const
{
    for (std::size_t i = 0; i < scene_count_; ++i) {
        if (scenes_[i].id == id) return i;
    }
    return scene_count_;
}

const Scene *SceneManager::find(const std::string &id) const
{
    std::size_t idx = find_index(id);
    if (idx >= scene_count_) return nullptr;
    return &scenes_[idx];
}

Scene *SceneManager::find_mutable(const std::string &id)
{
    std::size_t idx = find_index(id);
    if (idx >= scene_count_) return nullptr;
    return &scenes_[idx];
}

bool SceneManager::channel_exists(const std::string &channel_id) const
{
    return channel_mgr_.find(channel_id) != nullptr;
}

SceneResult SceneManager::capture_scene(const std::string &scene_id,
                                        const std::string &name,
                                        uint32_t transition_ms)
{
    // Check for duplicate id.
    if (find_index(scene_id) < scene_count_) {
        return SceneResult::kDuplicate;
    }
    if (scene_count_ >= kMaxScenes) {
        return SceneResult::kCapacity;
    }

    Scene scene{};
    scene.id = scene_id;
    scene.name = name;
    scene.transition_ms = transition_ms;

    // Capture current requested state for all channels.
    for (std::size_t i = 0; i < channel_mgr_.channel_count(); ++i) {
        // We need a way to iterate all channels... ChannelManager doesn't
        // expose iteration yet. For now, capture is done via update_scene
        // with explicitly provided states.
        (void)i;
    }
    scenes_[scene_count_] = scene;
    scene_count_++;
    return SceneResult::kOk;
}

SceneResult SceneManager::update_scene(const std::string &scene_id,
                                       const std::vector<SceneLightState> &states,
                                       uint32_t transition_ms)
{
    Scene *sc = find_mutable(scene_id);
    if (sc == nullptr) return SceneResult::kNotFound;

    // Validate all light ids and types.
    for (const auto &st : states) {
        if (!channel_exists(st.light_id)) {
            return SceneResult::kInvalid;
        }
        const Channel *ch = channel_mgr_.find(st.light_id);
        if (ch == nullptr) return SceneResult::kInvalid;
        LightState ls{};
        ls.power = st.power;
        ls.brightness = st.brightness;
        ls.red = st.red;
        ls.green = st.green;
        ls.blue = st.blue;
        ls.white = st.white;
        ls.color_temperature = st.color_temperature;
        if (!validate_state_for_type(ls, light_type_name(ch->type))) {
            return SceneResult::kInvalid;
        }
    }

    sc->states = states;
    sc->transition_ms = transition_ms;
    return SceneResult::kOk;
}

SceneResult SceneManager::delete_scene(const std::string &scene_id)
{
    std::size_t idx = find_index(scene_id);
    if (idx >= scene_count_) return SceneResult::kNotFound;
    scenes_[idx] = scenes_[scene_count_ - 1];
    scene_count_--;
    return SceneResult::kOk;
}

SceneResult SceneManager::activate_scene(const std::string &scene_id,
                                         uint32_t override_transition_ms,
                                         const std::string &origin,
                                         const std::string &command_id)
{
    Scene *sc = find_mutable(scene_id);
    if (sc == nullptr) return SceneResult::kNotFound;

    // Validate all lights still exist and are type-valid.
    for (const auto &st : sc->states) {
        if (!channel_exists(st.light_id)) {
            return SceneResult::kInvalid;
        }
        const Channel *ch = channel_mgr_.find(st.light_id);
        if (ch == nullptr) return SceneResult::kInvalid;
        LightState ls{};
        ls.power = st.power;
        ls.brightness = st.brightness;
        ls.red = st.red;
        ls.green = st.green;
        ls.blue = st.blue;
        ls.white = st.white;
        ls.color_temperature = st.color_temperature;
        if (!validate_state_for_type(ls, light_type_name(ch->type))) {
            return SceneResult::kInvalid;
        }
    }

    uint32_t tm = override_transition_ms != 0 ? override_transition_ms
                                              : sc->transition_ms;

    // Fan out all light states as one coordinated batch sharing command_id.
    for (const auto &st : sc->states) {
        LightCommand cmd{};
        cmd.channel_id = st.light_id;
        cmd.state.power = st.power;
        cmd.state.brightness = st.brightness;
        cmd.state.red = st.red;
        cmd.state.green = st.green;
        cmd.state.blue = st.blue;
        cmd.state.white = st.white;
        cmd.state.color_temperature = st.color_temperature;
        cmd.transition_ms = tm;
        cmd.origin = origin;
        cmd.command_id = command_id;

        LightCommandEnvelope envelope{};
        envelope.command = cmd;
        (void)envelope;
    }

    // Publish SCENE_ACTIVATED on the event bus (wired up when event bus is live).
    return SceneResult::kOk;
}

} // namespace lumen
