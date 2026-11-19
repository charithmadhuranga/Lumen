// Lumen — scene manager (include/lumen/lighting/scene_manager.h)
//
// Scenes save and restore lighting states with transitions (SPEC §9.3).
// Scene activation fans out to the engine as one coordinated command batch
// sharing a command_id (Phase 11 conflict dedup) and publishes
// SCENE_ACTIVATED on the event bus.
//
// Scene documents carry schema_version and go through the transactional
// config store. Unknown light ids are rejected at save time — no partial
// broken scenes are persisted.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "lumen/lighting/channel.h"
#include "lumen/lighting/lighting_task.h"

namespace lumen
{

// Result of scene operations.
enum class SceneResult : uint8_t
{
    kOk = 0,
    kNotFound,
    kDuplicate,
    kInvalid,      // unknown light id or unsupported property
    kCapacity,
};

// A single light's state within a scene.
struct SceneLightState
{
    std::string light_id;    // channel id
    bool power = false;
    uint8_t brightness = 0;  // 0..100
    uint8_t red = 0;         // 0..255
    uint8_t green = 0;       // 0..255
    uint8_t blue = 0;        // 0..255
    uint8_t white = 0;       // 0..255 (RGBW only)
    uint16_t color_temperature = 0; // 1000..20000 (CCT only)
};

// A scene: saved light states with a transition.
struct Scene
{
    std::string id;                // unique scene id
    std::string name;              // human-readable name
    uint32_t transition_ms = 0;    // default transition when activating
    std::vector<SceneLightState> states; // per-light states
};

// Scene manager: CRUD + activation fan-out through the lighting task.
class SceneManager
{
  public:
    static constexpr std::size_t kMaxScenes = 64;

    SceneManager(ChannelManager &channel_mgr, LightingTask &lighting_task);

    SceneResult register_scene(const Scene &scene);

    const Scene *find(const std::string &id) const;
    Scene *find_mutable(const std::string &id);

    std::size_t scene_count() const { return scene_count_; }

    // Save current light states as a scene (capture from channel manager).
    SceneResult capture_scene(const std::string &scene_id,
                              const std::string &name,
                              uint32_t transition_ms);

    // Update an existing scene's states.
    SceneResult update_scene(const std::string &scene_id,
                             const std::vector<SceneLightState> &states,
                             uint32_t transition_ms);

    // Delete a scene.
    SceneResult delete_scene(const std::string &scene_id);

    // Activate a scene: fan out all light states as one coordinated batch
    // sharing a command_id. Publishes SCENE_ACTIVATED on the bus.
    SceneResult activate_scene(const std::string &scene_id,
                               uint32_t override_transition_ms = 0,
                               const std::string &origin = "scene",
                               const std::string &command_id = "");

  private:
    ChannelManager &channel_mgr_;
    LightingTask &lighting_task_;

    Scene scenes_[kMaxScenes]{};
    std::size_t scene_count_ = 0;

    std::size_t find_index(const std::string &id) const;
    bool channel_exists(const std::string &channel_id) const;
};

} // namespace lumen
