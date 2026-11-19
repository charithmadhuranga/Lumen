// Lumen — lighting command queue and task (include/lumen/lighting/lighting_task.h)
//
// The lighting service is the sole executor of lighting commands. Producers
// (REST/WS/MQTT/inputs/automation) enqueue LightCommand envelopes into a
// bounded queue; the lighting task dequeues and applies them with transitions
// and effects, then writes to hardware via driver adapters.
//
// All hardware access goes through driver adapters (no GPIO in lighting code).
// Commands carry origin + command_id for Phase 11 conflict resolution.
//
// Pure C++ core (host-testable); FreeRTOS task wrapper lives in the .cpp.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <array>

#include "lumen/lighting/transition.h"
#include "lumen/lighting/channel.h"
#include "lumen/lighting/channel.h"

namespace lumen
{

// Maximum number of commands the queue can hold (bounded, no dynamic alloc).
static constexpr std::size_t kLightingCommandQueueSize = 32;

// Maximum number of concurrent transitions/effects (one per channel).
static constexpr std::size_t kLightingMaxChannels = 32;

// A command pushed to the lighting task by producers.
struct LightCommandEnvelope
{
    LightCommand command;
    uint32_t posted_ms = 0; // uptime when posted
};

// Result of trying to enqueue a command.
enum class EnqueueResult : uint8_t
{
    kOk = 0,
    kQueueFull, // bounded queue — caller must drop or retry
};

// Lighting task interface (host-testable).
class LightingTask
{
  public:
    LightingTask() = default;

    // Enqueue a command for the lighting task to apply.
    // Returns kQueueFull when the queue is at capacity.
    EnqueueResult enqueue(const LightCommandEnvelope &cmd);

    // Process up to `max_commands` pending commands.
    // Returns how many were processed.
    // `now_ms` is the current uptime (for transition timing).
    std::size_t process(uint32_t now_ms, std::size_t max_commands);

    // Get a channel's current requested state (after command processing).
    bool get_requested(const std::string &channel_id, LightState &out) const;

    // Get a channel's current applied state (last written to hardware).
    bool get_applied(const std::string &channel_id, LightState &out) const;

    // Register a channel with the lighting task (for state tracking).
    bool register_channel(const Channel &channel);

    // Get the channel manager (for driver adapters to resolve device_ids).
    ChannelManager &channel_manager()
    {
        return channel_manager_;
    }

  private:
    ChannelManager channel_manager_;

    // Bounded command queue (ring buffer).
    std::array<LightCommandEnvelope, kLightingCommandQueueSize> queue_{};
    std::size_t queue_head_ = 0;
    std::size_t queue_tail_ = 0;
    std::size_t queue_count_ = 0;

    // Active transitions (one per channel, indexed by channel slot).
    std::array<Transition, kLightingMaxChannels> transitions_{};
    std::size_t transition_slot_count_ = 0;

    // Apply a single command to hardware via driver adapters.
    // Returns true if hardware was written.
    bool apply_to_hardware(const LightCommand &cmd,
                           const LightState &new_state,
                           uint32_t now_ms);

    // Find the transition slot for a channel (or allocate one).
    std::size_t find_transition_slot(const std::string &channel_id);
};

} // namespace lumen
